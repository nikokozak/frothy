package serial

import (
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
	"golang.org/x/sys/unix"
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

// Port wraps a Darwin serial connection with byte-level and frame-level I/O.
type Port struct {
	file        *os.File
	path        string
	readTimeout time.Duration
}

// Open opens a serial port at the given path with Froth defaults (115200 8N1).
func Open(path string) (*Port, error) {
	fd, err := unix.Open(path, unix.O_RDWR|unix.O_NOCTTY|unix.O_NONBLOCK, 0)
	if err != nil {
		return nil, fmt.Errorf("open serial port: %w", err)
	}

	if err := configurePort(fd); err != nil {
		_ = unix.Close(fd)
		return nil, fmt.Errorf("configure serial port: %w", err)
	}

	return &Port{
		file:        os.NewFile(uintptr(fd), path),
		path:        path,
		readTimeout: 0,
	}, nil
}

func configurePort(fd int) error {
	settings, err := unix.IoctlGetTermios(fd, unix.TIOCGETA)
	if err != nil {
		return err
	}

	// Match the raw termios path that successfully reacquires the prompt and
	// enters control mode on the ESP32 board.
	settings.Iflag = 0
	settings.Oflag = 0
	settings.Cflag = unix.CS8 | unix.CREAD | unix.CLOCAL
	settings.Lflag = 0
	settings.Ispeed = unix.B115200
	settings.Ospeed = unix.B115200
	settings.Cc[unix.VMIN] = 0
	settings.Cc[unix.VTIME] = 0

	return unix.IoctlSetTermios(fd, unix.TIOCSETA, settings)
}

// Close closes the serial port.
func (p *Port) Close() error {
	if p.file == nil {
		return nil
	}
	return p.file.Close()
}

// Write sends raw bytes to the serial port.
func (p *Port) Write(data []byte) error {
	fd := int(p.file.Fd())
	for written := 0; written < len(data); {
		n, err := unix.Write(fd, data[written:])
		if err == unix.EINTR {
			continue
		}
		if err != nil {
			return fmt.Errorf("serial write: %w", err)
		}
		if n <= 0 {
			return fmt.Errorf("short write: wrote %d of %d bytes", written, len(data))
		}
		written += n
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
	fd := int(p.file.Fd())

	for {
		timeoutMs := durationToPollTimeout(p.readTimeout)
		fds := []unix.PollFd{{
			Fd:     int32(fd),
			Events: unix.POLLIN,
		}}

		n, err := unix.Poll(fds, timeoutMs)
		if err == unix.EINTR {
			continue
		}
		if err != nil {
			return 0, err
		}
		if n == 0 {
			return 0, nil
		}

		if fds[0].Revents&(unix.POLLERR|unix.POLLHUP|unix.POLLNVAL) != 0 {
			return 0, io.EOF
		}

		read, err := unix.Read(fd, buf)
		if err == unix.EINTR {
			continue
		}
		if err == unix.EAGAIN || err == unix.EWOULDBLOCK {
			continue
		}
		return read, err
	}
}

func durationToPollTimeout(timeout time.Duration) int {
	if timeout < 0 {
		return -1
	}
	if timeout == 0 {
		return 0
	}

	ms := int(math.Ceil(float64(timeout) / float64(time.Millisecond)))
	if ms <= 0 {
		return 1
	}
	return ms
}

// SetReadTimeout sets the read timeout on the serial port.
func (p *Port) SetReadTimeout(d time.Duration) error {
	p.readTimeout = d
	return nil
}

// Path returns the serial port's device path.
func (p *Port) Path() string {
	return p.path
}

// ResetInputBuffer flushes the OS-level serial input buffer.
func (p *Port) ResetInputBuffer() {
	_ = unix.IoctlSetPointerInt(int(p.file.Fd()), unix.TIOCFLUSH, unix.TCIFLUSH)
}

// Drain reads and discards all bytes for the given duration.
func (p *Port) Drain(duration time.Duration) {
	deadline := time.Now().Add(duration)
	buf := make([]byte, 256)
	for time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		_ = p.SetReadTimeout(remaining)
		_, _ = p.Read(buf)
	}
}
