package protocol

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"os"
	"strconv"
)

// Frame layout constants matching ADR-048 and froth_transport.h.
const (
	HeaderSize        = 20
	DefaultMaxPayload = 1024
	Magic0            = 'F'
	Magic1            = 'L'
	ProtocolVersion   = 2
)

var MaxPayload = configuredMaxPayload()

func configuredMaxPayload() int {
	value := os.Getenv("FROTH_LINK_MAX_PAYLOAD")
	if value == "" {
		return DefaultMaxPayload
	}

	n, err := strconv.Atoi(value)
	if err != nil || n <= 0 || n > 0xffff {
		panic(fmt.Sprintf("invalid FROTH_LINK_MAX_PAYLOAD=%q", value))
	}
	return n
}

// Message types (ADR-048 v2). 0x00 reserved (COBS delimiter).
const (
	HelloReq     = 0x01
	HelloRes     = 0x02
	AttachReq    = 0x03
	AttachRes    = 0x04
	DetachReq    = 0x05
	DetachRes    = 0x06
	InfoReq      = 0x07
	InfoRes      = 0x08
	ResetReq     = 0x09
	ResetRes     = 0x0A
	EvalReq      = 0x0B
	EvalRes      = 0x0C
	InterruptReq = 0x0D
	Keepalive    = 0x0E
	InputData    = 0x0F
	InputWait    = 0x10
	OutputData   = 0x11
	Error        = 0xFF
)

// Attach response status codes (ADR-048 section 3).
const (
	AttachStatusOK          = 0
	AttachStatusBusy        = 1
	AttachStatusUnsupported = 2
	AttachStatusInvalid     = 3
)

// Header represents a parsed FROTH-LINK/2 frame header.
type Header struct {
	Magic         [2]byte
	Version       byte
	MessageType   byte
	SessionID     uint64
	Seq           uint16
	PayloadLength uint16
	CRC32         uint32
}

// BuildFrame constructs a complete raw frame (header + payload) with
// computed CRC32. Returns the frame bytes ready for COBS encoding.
// This mirrors froth_link_header_build in froth_transport.c.
func BuildFrame(sessionID uint64, msgType byte, seq uint16, payload []byte) ([]byte, error) {
	// Frame layout (20-byte header + N payload bytes):
	//   [0..1]   magic "FL"
	//   [2]      version = 2
	//   [3]      message_type
	//   [4..11]  session_id (LE u64)
	//   [12..13] seq (LE u16)
	//   [14..15] payload_length (LE u16)
	//   [16..19] crc32 (LE u32)
	//   [20..N]  payload
	//
	// CRC32 covers header[0..15] concatenated with payload.
	// This is IEEE CRC32 (same polynomial as Go's crc32.IEEE).

	plen := len(payload)
	if plen > MaxPayload {
		return nil, fmt.Errorf("payload too large: %d > %d", plen, MaxPayload)
	}

	frame := make([]byte, HeaderSize+plen)

	// Header fields
	frame[0] = Magic0
	frame[1] = Magic1
	frame[2] = ProtocolVersion
	frame[3] = msgType
	binary.LittleEndian.PutUint64(frame[4:12], sessionID)
	binary.LittleEndian.PutUint16(frame[12:14], seq)
	binary.LittleEndian.PutUint16(frame[14:16], uint16(plen))

	// Copy payload
	copy(frame[HeaderSize:], payload)

	// CRC32 over header[0..15] + payload
	crcData := make([]byte, 16+plen)
	copy(crcData[:16], frame[:16])
	copy(crcData[16:], payload)
	checksum := crc32.ChecksumIEEE(crcData)
	binary.LittleEndian.PutUint32(frame[16:20], checksum)

	return frame, nil
}

// ParseFrame validates and parses a decoded (post-COBS) frame.
// Returns the header and payload slice, or an error.
// This mirrors froth_link_header_parse in froth_transport.c.
func ParseFrame(frame []byte) (*Header, []byte, error) {
	// Validation order (matches device side):
	// 1. Frame must be at least 20 bytes (header size).
	// 2. Magic must be "FL".
	// 3. Version must be 2.
	// 4. Read message_type, session_id, seq, payload_length, crc32 (all LE).
	// 5. payload_length must not exceed MaxPayload.
	// 6. Frame must be at least HeaderSize + payload_length bytes.
	// 7. Compute CRC32 over header[0..15] + payload. Must match.

	if len(frame) < HeaderSize {
		return nil, nil, fmt.Errorf("frame too short: %d bytes", len(frame))
	}

	if frame[0] != Magic0 || frame[1] != Magic1 {
		return nil, nil, fmt.Errorf("bad magic: %c%c", frame[0], frame[1])
	}

	if frame[2] != ProtocolVersion {
		return nil, nil, fmt.Errorf("unsupported version: %d", frame[2])
	}

	h := &Header{
		Magic:         [2]byte{frame[0], frame[1]},
		Version:       frame[2],
		MessageType:   frame[3],
		SessionID:     binary.LittleEndian.Uint64(frame[4:12]),
		Seq:           binary.LittleEndian.Uint16(frame[12:14]),
		PayloadLength: binary.LittleEndian.Uint16(frame[14:16]),
		CRC32:         binary.LittleEndian.Uint32(frame[16:20]),
	}

	if int(h.PayloadLength) > MaxPayload {
		return nil, nil, fmt.Errorf("payload too large: %d", h.PayloadLength)
	}

	total := HeaderSize + int(h.PayloadLength)
	if len(frame) != total {
		return nil, nil, fmt.Errorf("frame size mismatch: need %d, have %d", total, len(frame))
	}

	payload := frame[HeaderSize:total]

	// CRC check
	crcData := make([]byte, 16+len(payload))
	copy(crcData[:16], frame[:16])
	copy(crcData[16:], payload)
	expected := crc32.ChecksumIEEE(crcData)

	if expected != h.CRC32 {
		return nil, nil, fmt.Errorf("CRC mismatch: expected %08x, got %08x", expected, h.CRC32)
	}

	return h, payload, nil
}

// EncodeWireFrame builds a complete wire frame: 0x00 + COBS(raw) + 0x00.
// This is what gets written to the serial port.
func EncodeWireFrame(sessionID uint64, msgType byte, seq uint16, payload []byte) ([]byte, error) {
	raw, err := BuildFrame(sessionID, msgType, seq, payload)
	if err != nil {
		return nil, err
	}

	encoded := COBSEncode(raw)

	// Wire format: 0x00 delimiter + encoded + 0x00 delimiter
	wire := make([]byte, 1+len(encoded)+1)
	wire[0] = 0x00
	copy(wire[1:], encoded)
	wire[len(wire)-1] = 0x00

	return wire, nil
}
