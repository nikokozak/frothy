package protocol

import (
	"bytes"
	"testing"
)

func TestDefaultMaxPayloadAllowsWorkshopDefinitions(t *testing.T) {
	if DefaultMaxPayload != 1024 {
		t.Fatalf("DefaultMaxPayload = %d, want 1024", DefaultMaxPayload)
	}
	if MaxPayload < 1024 {
		t.Fatalf("MaxPayload = %d, want at least 1024", MaxPayload)
	}

	payload := bytes.Repeat([]byte{'x'}, 596)
	frame, err := BuildFrame(1, EvalReq, 1, payload)
	if err != nil {
		t.Fatalf("BuildFrame workshop-sized payload: %v", err)
	}
	if len(frame) != HeaderSize+len(payload) {
		t.Fatalf("frame length = %d, want %d", len(frame), HeaderSize+len(payload))
	}
}
