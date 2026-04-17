package serial

import (
	"bytes"
	"io"
	"testing"
	"time"
)

type frameTestTransport struct {
	reads        [][]byte
	readCalls    int
	readTimeouts []time.Duration
}

func (t *frameTestTransport) Read(buf []byte) (int, error) {
	if t.readCalls >= len(t.reads) {
		return 0, io.EOF
	}
	data := t.reads[t.readCalls]
	t.readCalls++
	if len(data) == 0 {
		return 0, nil
	}
	return copy(buf, data), nil
}

func (t *frameTestTransport) Write([]byte) error { return nil }

func (t *frameTestTransport) Close() error { return nil }

func (t *frameTestTransport) Path() string { return "/dev/mock" }

func (t *frameTestTransport) SetReadTimeout(d time.Duration) error {
	t.readTimeouts = append(t.readTimeouts, d)
	return nil
}

func (t *frameTestTransport) ResetInputBuffer() {}

func (t *frameTestTransport) Drain(time.Duration) {}

func TestReadFrameTransportContinuesAfterFiniteReadSlice(t *testing.T) {
	transport := &frameTestTransport{
		reads: [][]byte{
			nil,
			{0x00},
			{'a'},
			{0x00},
		},
	}

	frame, err := ReadFrameTransport(transport, time.Second, nil)
	if err != nil {
		t.Fatalf("ReadFrameTransport: %v", err)
	}
	if !bytes.Equal(frame, []byte{'a'}) {
		t.Fatalf("frame = %q, want %q", frame, []byte{'a'})
	}
	if transport.readCalls != 4 {
		t.Fatalf("read calls = %d, want 4", transport.readCalls)
	}
	if len(transport.readTimeouts) == 0 {
		t.Fatalf("read timeout was not set")
	}
	for _, timeout := range transport.readTimeouts {
		if timeout > 250*time.Millisecond {
			t.Fatalf("read timeout = %v, want capped slice", timeout)
		}
	}
}
