package frothycontrol

import (
	"encoding/binary"
	"fmt"

	baseproto "github.com/nikokozak/frothy/tools/cli/internal/protocol"
)

const (
	helloReq     = 0x01
	helloEvt     = 0x02
	evalReq      = 0x03
	wordsReq     = 0x04
	seeReq       = 0x05
	detachReq    = 0x06
	resetReq     = 0x07
	saveReq      = 0x08
	restoreReq   = 0x09
	wipeReq      = 0x0A
	coreReq      = 0x0B
	slotInfoReq  = 0x0C
	outputEvt    = 0x10
	valueEvt     = 0x11
	errorEvt     = 0x12
	interruptEvt = 0x13
	idleEvt      = 0x14
)

const (
	phaseControl = 1
	phaseParse   = 2
	phaseEval    = 3
	phaseInspect = 4
)

type ControlError struct {
	Phase  uint8
	Code   uint16
	Detail string
}

func (e *ControlError) Error() string {
	return fmt.Sprintf("control error phase=%d code=%d detail=%q", e.Phase, e.Code, e.Detail)
}

type SeeResult struct {
	Name       string `json:"name"`
	IsOverlay  bool   `json:"is_overlay"`
	ValueClass uint8  `json:"value_class"`
	Rendered   string `json:"rendered"`
}

func buildStringPayload(text string) []byte {
	buf := make([]byte, 2+len(text))
	binary.LittleEndian.PutUint16(buf[:2], uint16(len(text)))
	copy(buf[2:], text)
	return buf
}

func parseStringPayload(payload []byte) (string, error) {
	if len(payload) < 2 {
		return "", fmt.Errorf("payload too short")
	}
	length := int(binary.LittleEndian.Uint16(payload[:2]))
	if len(payload) != 2+length {
		return "", fmt.Errorf("payload length mismatch: want %d, have %d", 2+length, len(payload))
	}
	return string(payload[2:]), nil
}

func parseWordsChunkPayload(payload []byte) ([]string, error) {
	reader := &payloadReader{data: payload}
	count := int(reader.u16())
	names := make([]string, 0, count)

	for i := 0; i < count; i++ {
		names = append(names, reader.str())
	}
	if reader.err != nil {
		return nil, fmt.Errorf("parse WORDS value: %w", reader.err)
	}
	if reader.pos != len(reader.data) {
		return nil, fmt.Errorf("parse WORDS value: trailing payload")
	}
	return names, nil
}

func parseWordsPayloads(payloads [][]byte) ([]string, error) {
	if len(payloads) == 0 {
		return nil, fmt.Errorf("missing WORDS value")
	}

	var names []string
	for _, payload := range payloads {
		chunk, err := parseWordsChunkPayload(payload)
		if err != nil {
			return nil, err
		}
		names = append(names, chunk...)
	}
	return names, nil
}

func parseSeeChunkPayload(payload []byte) (*SeeResult, error) {
	reader := &payloadReader{data: payload}
	result := &SeeResult{
		Name:       reader.str(),
		IsOverlay:  reader.u8() != 0,
		ValueClass: reader.u8(),
		Rendered:   reader.str(),
	}
	if reader.err != nil {
		return nil, fmt.Errorf("parse SEE value: %w", reader.err)
	}
	if reader.pos != len(reader.data) {
		return nil, fmt.Errorf("parse SEE value: trailing payload")
	}
	return result, nil
}

func parseSeePayloads(payloads [][]byte) (*SeeResult, error) {
	if len(payloads) == 0 {
		return nil, fmt.Errorf("missing SEE value")
	}

	result, err := parseSeeChunkPayload(payloads[0])
	if err != nil {
		return nil, err
	}

	for _, payload := range payloads[1:] {
		chunk, err := parseSeeChunkPayload(payload)
		if err != nil {
			return nil, err
		}
		if chunk.Name != result.Name ||
			chunk.IsOverlay != result.IsOverlay ||
			chunk.ValueClass != result.ValueClass {
			return nil, fmt.Errorf("parse SEE value: mismatched chunk header")
		}
		result.Rendered += chunk.Rendered
	}

	return result, nil
}

func parseControlError(payload []byte) (*ControlError, error) {
	reader := &payloadReader{data: payload}
	errEvt := &ControlError{
		Phase:  reader.u8(),
		Code:   reader.u16(),
		Detail: reader.str(),
	}
	if reader.err != nil {
		return nil, fmt.Errorf("parse ERROR event: %w", reader.err)
	}
	if reader.pos != len(reader.data) {
		return nil, fmt.Errorf("parse ERROR event: trailing payload")
	}
	return errEvt, nil
}

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
	value := r.data[r.pos]
	r.pos++
	return value
}

func (r *payloadReader) u16() uint16 {
	if r.err != nil || r.pos+2 > len(r.data) {
		r.err = fmt.Errorf("payload underflow at offset %d", r.pos)
		return 0
	}
	value := binary.LittleEndian.Uint16(r.data[r.pos:])
	r.pos += 2
	return value
}

func (r *payloadReader) str() string {
	length := int(r.u16())
	if r.err != nil {
		return ""
	}
	if r.pos+length > len(r.data) {
		r.err = fmt.Errorf("string overflows payload at offset %d", r.pos)
		return ""
	}
	text := string(r.data[r.pos : r.pos+length])
	r.pos += length
	return text
}

func encodeWireFrame(sessionID uint64, msgType byte, seq uint16, payload []byte) ([]byte, error) {
	return baseproto.EncodeWireFrame(sessionID, msgType, seq, payload)
}

func decodeFrame(encoded []byte) (*baseproto.Header, []byte, error) {
	decoded, err := baseproto.COBSDecode(encoded)
	if err != nil {
		return nil, nil, err
	}
	return baseproto.ParseFrame(decoded)
}
