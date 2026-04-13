package frothycontrol

import (
	"bytes"
	"encoding/binary"
	"errors"
	"testing"
	"time"

	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
)

type stubTransport struct {
	readBuf bytes.Buffer
	writes  [][]byte
}

func (t *stubTransport) Read(buf []byte) (int, error) {
	if t.readBuf.Len() == 0 {
		return 0, nil
	}
	return t.readBuf.Read(buf)
}

func (t *stubTransport) Write(data []byte) error {
	t.writes = append(t.writes, append([]byte(nil), data...))
	return nil
}

func (t *stubTransport) Close() error { return nil }

func (t *stubTransport) Path() string { return "stub" }

func (t *stubTransport) SetReadTimeout(time.Duration) error { return nil }

func (t *stubTransport) ResetInputBuffer() {}

func (t *stubTransport) Drain(time.Duration) {}

func (t *stubTransport) queueFrame(tst *testing.T, sessionID uint64, msgType byte,
	seq uint16, payload []byte) {
	tst.Helper()

	wire, err := encodeWireFrame(sessionID, msgType, seq, payload)
	if err != nil {
		tst.Fatalf("encode frame: %v", err)
	}
	if _, err := t.readBuf.Write(wire); err != nil {
		tst.Fatalf("queue frame: %v", err)
	}
}

func buildWordsChunkPayload(names ...string) []byte {
	payload := make([]byte, 2)
	binary.LittleEndian.PutUint16(payload[:2], uint16(len(names)))
	for _, name := range names {
		payload = append(payload, buildStringPayload(name)...)
	}
	return payload
}

func buildSeeChunkPayload(name string, isOverlay bool, valueClass uint8,
	rendered string) []byte {
	payload := append([]byte(nil), buildStringPayload(name)...)
	if isOverlay {
		payload = append(payload, 1)
	} else {
		payload = append(payload, 0)
	}
	payload = append(payload, valueClass)
	payload = append(payload, buildStringPayload(rendered)...)
	return payload
}

func buildControlErrorPayload(phase uint8, code uint16, detail string) []byte {
	payload := []byte{phase, 0, 0}
	binary.LittleEndian.PutUint16(payload[1:3], code)
	return append(payload, buildStringPayload(detail)...)
}

func TestSessionWordsAccumulatesValueChunks(t *testing.T) {
	transport := &stubTransport{}
	session := NewSession(transport)
	session.sessionID = 0x42
	session.nextSeq = 1

	transport.queueFrame(t, session.sessionID, valueEvt, 1,
		buildWordsChunkPayload("save", "restore"))
	transport.queueFrame(t, session.sessionID, valueEvt, 1,
		buildWordsChunkPayload("control.demo"))
	transport.queueFrame(t, session.sessionID, idleEvt, 1, nil)

	names, err := session.Words(100 * time.Millisecond)
	if err != nil {
		t.Fatalf("Words: %v", err)
	}
	if len(names) != 3 {
		t.Fatalf("Words count = %d, want 3 (%v)", len(names), names)
	}
	if names[0] != "save" || names[1] != "restore" || names[2] != "control.demo" {
		t.Fatalf("Words names = %v", names)
	}
}

func TestSessionSeeAccumulatesRenderedChunks(t *testing.T) {
	transport := &stubTransport{}
	session := NewSession(transport)
	session.sessionID = 0x42
	session.nextSeq = 1

	transport.queueFrame(t, session.sessionID, valueEvt, 1,
		buildSeeChunkPayload("bigText", true, 3, "\"aaaa"))
	transport.queueFrame(t, session.sessionID, valueEvt, 1,
		buildSeeChunkPayload("bigText", true, 3, "bbbb\""))
	transport.queueFrame(t, session.sessionID, idleEvt, 1, nil)

	view, err := session.See("bigText", 100*time.Millisecond)
	if err != nil {
		t.Fatalf("See: %v", err)
	}
	if view.Name != "bigText" || !view.IsOverlay || view.ValueClass != 3 {
		t.Fatalf("See header = %+v", view)
	}
	if view.Rendered != "\"aaaabbbb\"" {
		t.Fatalf("See rendered = %q", view.Rendered)
	}
}

func TestManagerResetMapsUnknownRequestToResetUnavailable(t *testing.T) {
	transport := &stubTransport{}
	session := NewSession(transport)
	session.sessionID = 0x55
	session.nextSeq = 1

	transport.queueFrame(t, session.sessionID, errorEvt, 1,
		buildControlErrorPayload(phaseControl, 256, "unknown request"))
	transport.queueFrame(t, session.sessionID, idleEvt, 1, nil)

	manager := NewManager(ManagerConfig{})
	manager.conn = &managedConnection{
		session: session,
		info: &baseproto.HelloResponse{
			Board:   "mock-board",
			Version: "0.1.0-test",
		},
	}

	if _, err := manager.Reset(); !errors.Is(err, ErrResetUnavailable) {
		t.Fatalf("Reset error = %v, want ErrResetUnavailable", err)
	}
}
