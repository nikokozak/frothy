package protocol

import (
	"crypto/rand"
	"encoding/binary"
	"fmt"
)

// All payload formats use little-endian integers and length-prefixed
// strings (u16 length + raw UTF-8 bytes). These match the binary
// payloads in froth_link.c (device-side dispatcher).

// --- HELLO ---

// HelloResponse holds parsed HELLO_RES payload fields.
type HelloResponse struct {
	CellBits     uint8
	MaxPayload   uint16
	HeapSize     uint32
	HeapUsed     uint32
	SlotCount    uint16
	Flags        uint8
	Version      string
	Board        string
	Capabilities []uint8
}

// ParseHelloResponse decodes a HELLO_RES binary payload.
func ParseHelloResponse(p []byte) (*HelloResponse, error) {
	// Payload layout (from froth_link.c handle_hello):
	//   u8   cell_bits
	//   u16  max_payload
	//   u32  heap_size
	//   u32  heap_used
	//   u16  slot_count
	//   u8   flags
	//   str  version        (u16 len + bytes)
	//   str  board          (u16 len + bytes)
	//   u8   capability_count
	//   u8   capabilities[] (each: u8 capability_id, per ADR-033)

	r := &payloadReader{data: p}

	h := &HelloResponse{}
	h.CellBits = r.u8()
	h.MaxPayload = r.u16()
	h.HeapSize = r.u32()
	h.HeapUsed = r.u32()
	h.SlotCount = r.u16()
	h.Flags = r.u8()
	h.Version = r.str()
	h.Board = r.str()

	capCount := r.u8()
	for i := uint8(0); i < capCount; i++ {
		h.Capabilities = append(h.Capabilities, r.u8())
	}

	if r.err != nil {
		return nil, fmt.Errorf("parse HELLO_RES: %w", r.err)
	}
	return h, nil
}

// GenerateSessionID returns a cryptographically random non-zero uint64.
func GenerateSessionID() (uint64, error) {
	for {
		var buf [8]byte
		if _, err := rand.Read(buf[:]); err != nil {
			return 0, fmt.Errorf("generate session ID: %w", err)
		}
		id := binary.LittleEndian.Uint64(buf[:])
		if id != 0 {
			return id, nil
		}
	}
}

// ParseAttachResponse reads ATTACH_RES payload: 1 byte status.
func ParseAttachResponse(payload []byte) (uint8, error) {
	if len(payload) != 1 {
		return 0, fmt.Errorf("attach response wrong size: %d bytes", len(payload))
	}
	return payload[0], nil
}

// --- EVAL ---

// BuildEvalPayload constructs an EVAL_REQ binary payload.
func BuildEvalPayload(source string) []byte {
	// Payload layout (from froth_link.c handle_eval):
	//   u8   flags (0 for now)
	//   u16  source_len
	//   []   source bytes (raw UTF-8, NOT null-terminated)

	src := []byte(source)
	buf := make([]byte, 1+2+len(src))
	buf[0] = 0 // flags
	binary.LittleEndian.PutUint16(buf[1:3], uint16(len(src)))
	copy(buf[3:], src)
	return buf
}

// ParseOutputData reads OUTPUT_DATA payload: u16le byte_count + raw bytes.
func ParseOutputData(payload []byte) ([]byte, error) {
	if len(payload) < 2 {
		return nil, fmt.Errorf("output data too short: %d bytes", len(payload))
	}
	count := binary.LittleEndian.Uint16(payload[:2])
	if int(count) != len(payload)-2 {
		return nil, fmt.Errorf("output data truncated: want %d, have %d", count, len(payload)-2)
	}
	return payload[2 : 2+count], nil
}

// ParseInputWait reads INPUT_WAIT payload: 1 byte reason.
func ParseInputWait(payload []byte) (uint8, error) {
	if len(payload) != 1 {
		return 0, fmt.Errorf("input wait wrong size: %d bytes", len(payload))
	}
	return payload[0], nil
}

// BuildInputDataPayload builds INPUT_DATA: u16le byte_count + raw bytes.
func BuildInputDataPayload(data []byte) []byte {
	payload := make([]byte, 2+len(data))
	binary.LittleEndian.PutUint16(payload[:2], uint16(len(data)))
	copy(payload[2:], data)
	return payload
}

// EvalResponse holds parsed EVAL_RES payload fields.
type EvalResponse struct {
	Status    uint8 // 0 = success, 1 = error
	ErrorCode uint16
	FaultWord string
	StackRepr string
}

// ParseEvalResponse decodes an EVAL_RES binary payload.
func ParseEvalResponse(p []byte) (*EvalResponse, error) {
	// Payload layout (from froth_link.c handle_eval):
	//   u8   status (0 = ok, 1 = error)
	//   u16  error_code
	//   str  fault_word
	//   str  stack_repr

	r := &payloadReader{data: p}

	e := &EvalResponse{}
	e.Status = r.u8()
	e.ErrorCode = r.u16()
	e.FaultWord = r.str()
	e.StackRepr = r.str()

	if r.err != nil {
		return nil, fmt.Errorf("parse EVAL_RES: %w", r.err)
	}
	return e, nil
}

// --- INFO ---

// InfoResponse holds parsed INFO_RES payload fields.
type InfoResponse struct {
	HeapSize         uint32
	HeapUsed         uint32
	HeapOverlayUsed  uint32
	SlotCount        uint16
	SlotOverlayCount uint16
	Flags            uint8
	Version          string
}

// ParseInfoResponse decodes an INFO_RES binary payload.
func ParseInfoResponse(p []byte) (*InfoResponse, error) {
	// Payload layout (from froth_link.c handle_info):
	//   u32  heap_size
	//   u32  heap_used
	//   u32  heap_overlay_used
	//   u16  slot_count
	//   u16  slot_overlay_count
	//   u8   flags
	//   str  version

	r := &payloadReader{data: p}

	info := &InfoResponse{}
	info.HeapSize = r.u32()
	info.HeapUsed = r.u32()
	info.HeapOverlayUsed = r.u32()
	info.SlotCount = r.u16()
	info.SlotOverlayCount = r.u16()
	info.Flags = r.u8()
	info.Version = r.str()

	if r.err != nil {
		return nil, fmt.Errorf("parse INFO_RES: %w", r.err)
	}
	return info, nil
}

// --- RESET ---

// ResetResponse holds parsed RESET_RES payload fields.
type ResetResponse struct {
	Status           uint32
	HeapSize         uint32
	HeapUsed         uint32
	HeapOverlayUsed  uint32
	SlotCount        uint16
	SlotOverlayCount uint16
	Flags            uint8
	Version          string
}

// ParseResetResponse decodes an RESET_RES binary payload.
func ParseResetResponse(p []byte) (*ResetResponse, error) {
	// Payload layout (from froth_link.c handle_reset):
	//   u32  status -- corresponds to the froth_error_t returned by froth_prim_dangerous_reset (0 if OK)
	//   u32  heap_size
	//   u32  heap_used
	//   u32  heap_overlay_used
	//   u16  slot_count
	//   u16  slot_overlay_count
	//   u8   flags
	//   str  version

	r := &payloadReader{data: p}

	reset := &ResetResponse{}
	reset.Status = r.u32()
	reset.HeapSize = r.u32()
	reset.HeapUsed = r.u32()
	reset.HeapOverlayUsed = r.u32()
	reset.SlotCount = r.u16()
	reset.SlotOverlayCount = r.u16()
	reset.Flags = r.u8()
	reset.Version = r.str()

	if r.err != nil {
		return nil, fmt.Errorf("parse RESET_RES: %w", r.err)
	}
	if reset.Status != 0 {
		return nil, fmt.Errorf("RESET_RES device reset error: %d", reset.Status)
	}
	return reset, nil
}

// --- ERROR ---

// ErrorResponse holds a parsed ERROR payload.
type ErrorResponse struct {
	Category uint8
	Detail   string
}

// ParseErrorResponse decodes an ERROR binary payload.
func ParseErrorResponse(p []byte) (*ErrorResponse, error) {
	r := &payloadReader{data: p}
	e := &ErrorResponse{}
	e.Category = r.u8()
	e.Detail = r.str()
	if r.err != nil {
		return nil, fmt.Errorf("parse ERROR: %w", r.err)
	}
	return e, nil
}

// --- Payload reader helper ---
// Cursor-based reader that tracks position and defers error checking.
// Same pattern as the payload_writer_t on the device side, but for reading.

type payloadReader struct {
	data []byte
	pos  int
	err  error
}

func (r *payloadReader) u8() uint8 {
	if r.err != nil || r.pos+1 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := r.data[r.pos]
	r.pos++
	return v
}

func (r *payloadReader) u16() uint16 {
	if r.err != nil || r.pos+2 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := binary.LittleEndian.Uint16(r.data[r.pos:])
	r.pos += 2
	return v
}

func (r *payloadReader) u32() uint32 {
	if r.err != nil || r.pos+4 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	v := binary.LittleEndian.Uint32(r.data[r.pos:])
	r.pos += 4
	return v
}

func (r *payloadReader) str() string {
	length := r.u16()
	if r.err != nil {
		return ""
	}
	if r.pos+int(length) > len(r.data) {
		r.err = fmt.Errorf("string overflows payload at offset %d (len=%d)", r.pos, length)
		return ""
	}
	s := string(r.data[r.pos : r.pos+int(length)])
	r.pos += int(length)
	return s
}
