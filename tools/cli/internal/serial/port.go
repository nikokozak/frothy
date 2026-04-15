//go:build !darwin

package serial

import (
	"errors"
	"fmt"
	"io"
	"time"

	"github.com/nikokozak/frothy/tools/cli/internal/protocol"
	"go.bug.st/serial"
)

var ErrTimeout = errors.New("serial read timeout")

var maxEncodedFrameSize = protocol.HeaderSize + protocol.MaxPayload +
	((protocol.HeaderSize + protocol.MaxPayload) / 254) + 1

// Transport is the shared host-side byte transport used by the direct
// session path and the daemon backends.
type Transport interface {
	Read(buf []byte) (int, error)
	Write(data []byte) error
	Close() error
	Path() string
	SetReadTimeout(d time.Duration) error
	ResetInputBuffer()
	Drain(duration time.Duration)
}

// Port wraps a serial connection with byte-level and frame-level I/O.
type Port struct {
	port serial.Port
	path string
}

// Open opens a serial port at the given path with Froth defaults (115200 8N1).
func Open(path string) (*Port, error) {
	conn, err := serial.Open(path, &serial.Mode{
		BaudRate: 115200,
		DataBits: 8,
		StopBits: serial.OneStopBit,
		Parity:   serial.NoParity,
		// Keep ESP32 auto-program lines deasserted after open. Leaving DTR/RTS
		// asserted is enough to strand boards in reset or ROM boot mode.
		InitialStatusBits: &serial.ModemOutputBits{
			DTR: false,
			RTS: false,
		},
	})
	if err != nil {
		return nil, fmt.Errorf("open serial port: %w", err)
	}
	return &Port{port: conn, path: path}, nil
}

// Close closes the serial port.
func (p *Port) Close() error {
	return p.port.Close()
}

// Write sends raw bytes to the serial port.
func (p *Port) Write(data []byte) error {
	n, err := p.port.Write(data)
	if err != nil {
		return fmt.Errorf("serial write: %w", err)
	}
	if n != len(data) {
		return fmt.Errorf("short write: wrote %d of %d bytes", n, len(data))
	}
	return nil
}

// ReadFrame reads bytes until a complete COBS frame is captured (bytes
// between two 0x00 delimiters). Non-frame bytes are discarded.
// Returns the raw COBS-encoded bytes (without delimiters).
func (p *Port) ReadFrame(timeout time.Duration) ([]byte, error) {
	return ReadFrameTransport(p, timeout, nil)
}

// ReadFrameTransport reads bytes until a complete COBS frame is captured
// (bytes between two 0x00 delimiters). Non-frame bytes are forwarded to
// passthrough when it is non-nil.
func ReadFrameTransport(conn Transport, timeout time.Duration, passthrough io.Writer) ([]byte, error) {
	noTimeout := timeout <= 0
	deadline := time.Now()
	if !noTimeout {
		deadline = time.Now().Add(timeout)
	}
	buf := make([]byte, 1)
	var frame []byte
	inFrame := false

	for {
		remaining := 30 * time.Second
		if !noTimeout {
			remaining = time.Until(deadline)
			if remaining <= 0 {
				return nil, ErrTimeout
			}
		}
		if err := conn.SetReadTimeout(remaining); err != nil {
			return nil, err
		}

		n, err := conn.Read(buf)
		if err != nil {
			return nil, fmt.Errorf("transport read: %w", err)
		}
		if n == 0 {
			if noTimeout {
				continue
			}
			return nil, ErrTimeout
		}

		b := buf[0]

		if b == 0x00 {
			if inFrame && len(frame) > 0 {
				return frame, nil
			}
			// Start (or restart) a new frame.
			frame = frame[:0]
			inFrame = true
			continue
		}

		if inFrame {
			if len(frame) >= maxEncodedFrameSize {
				frame = frame[:0]
				inFrame = false
				continue
			}
			frame = append(frame, b)
		} else if passthrough != nil {
			_, _ = passthrough.Write([]byte{b})
		}
	}
}

// Read reads raw bytes from the serial port.
func (p *Port) Read(buf []byte) (int, error) {
	return p.port.Read(buf)
}

// SetReadTimeout sets the read timeout on the serial port.
func (p *Port) SetReadTimeout(d time.Duration) error {
	return p.port.SetReadTimeout(d)
}

// Path returns the serial port's device path.
func (p *Port) Path() string {
	return p.path
}

// ResetInputBuffer flushes the OS-level serial input buffer.
func (p *Port) ResetInputBuffer() {
	p.port.ResetInputBuffer()
}

// Drain reads and discards all bytes for the given duration.
// Clears boot messages before sending the first frame.
func (p *Port) Drain(duration time.Duration) {
	deadline := time.Now().Add(duration)
	buf := make([]byte, 256)
	for time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		p.port.SetReadTimeout(remaining)
		p.port.Read(buf)
	}
}
