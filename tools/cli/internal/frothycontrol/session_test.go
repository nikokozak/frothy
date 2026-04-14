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

func buildOutputPayload(data []byte) []byte {
	payload := make([]byte, 2+len(data))
	binary.LittleEndian.PutUint16(payload[:2], uint16(len(data)))
	copy(payload[2:], data)
	return payload
}

func decodeWriteFrame(t *testing.T, wire []byte) (*baseproto.Header, []byte) {
	t.Helper()

	if len(wire) < 2 {
		t.Fatalf("wire frame too short: %d", len(wire))
	}
	header, payload, err := decodeFrame(wire[1 : len(wire)-1])
	if err != nil {
		t.Fatalf("decode write frame: %v", err)
	}
	return header, payload
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

func TestSessionDirectTextRequestsUseRequestSpecificOpcodes(t *testing.T) {
	transport := &stubTransport{}
	session := NewSession(transport)
	session.sessionID = 0x42
	session.nextSeq = 1

	transport.queueFrame(t, session.sessionID, valueEvt, 1, buildStringPayload("nil"))
	transport.queueFrame(t, session.sessionID, idleEvt, 1, nil)
	saveValue, err := session.Save(100*time.Millisecond, nil)
	if err != nil {
		t.Fatalf("Save: %v", err)
	}
	if saveValue != "nil" {
		t.Fatalf("Save value = %q", saveValue)
	}

	transport.queueFrame(t, session.sessionID, outputEvt, 2,
		buildOutputPayload([]byte("<native save/0>\n")))
	transport.queueFrame(t, session.sessionID, valueEvt, 2, buildStringPayload("nil"))
	transport.queueFrame(t, session.sessionID, idleEvt, 2, nil)

	var output bytes.Buffer
	coreValue, err := session.Core("save", 100*time.Millisecond, func(data []byte) {
		output.Write(data)
	})
	if err != nil {
		t.Fatalf("Core: %v", err)
	}
	if coreValue != "nil" {
		t.Fatalf("Core value = %q", coreValue)
	}
	if output.String() != "<native save/0>\n" {
		t.Fatalf("Core output = %q", output.String())
	}

	if len(transport.writes) != 2 {
		t.Fatalf("write count = %d, want 2", len(transport.writes))
	}

	saveHeader, savePayload := decodeWriteFrame(t, transport.writes[0])
	if saveHeader.MessageType != saveReq {
		t.Fatalf("Save request type = %d, want %d", saveHeader.MessageType, saveReq)
	}
	if len(savePayload) != 0 {
		t.Fatalf("Save payload len = %d, want 0", len(savePayload))
	}

	coreHeader, corePayload := decodeWriteFrame(t, transport.writes[1])
	if coreHeader.MessageType != coreReq {
		t.Fatalf("Core request type = %d, want %d", coreHeader.MessageType, coreReq)
	}
	name, err := parseStringPayload(corePayload)
	if err != nil {
		t.Fatalf("parse Core payload: %v", err)
	}
	if name != "save" {
		t.Fatalf("Core payload name = %q, want save", name)
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

func TestManagerBuiltinFallbacksUseEvalOnUnknownRequest(t *testing.T) {
	cases := []struct {
		name           string
		run            func(*Manager) (string, error)
		directReq      byte
		wantEvalSource string
	}{
		{
			name: "save",
			run: func(m *Manager) (string, error) {
				return m.Save(nil)
			},
			directReq:      saveReq,
			wantEvalSource: "save()",
		},
		{
			name: "restore",
			run: func(m *Manager) (string, error) {
				return m.Restore(nil)
			},
			directReq:      restoreReq,
			wantEvalSource: "restore()",
		},
		{
			name: "wipe",
			run: func(m *Manager) (string, error) {
				return m.Wipe(nil)
			},
			directReq:      wipeReq,
			wantEvalSource: "wipe()",
		},
		{
			name: "core",
			run: func(m *Manager) (string, error) {
				return m.Core("save", nil)
			},
			directReq:      coreReq,
			wantEvalSource: `core("save")`,
		},
		{
			name: "slot_info",
			run: func(m *Manager) (string, error) {
				return m.SlotInfo("save", nil)
			},
			directReq:      slotInfoReq,
			wantEvalSource: `slotInfo("save")`,
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			transport := &stubTransport{}
			session := NewSession(transport)
			session.sessionID = 0x55
			session.nextSeq = 1

			transport.queueFrame(t, session.sessionID, errorEvt, 1,
				buildControlErrorPayload(phaseControl, 256, "unknown request"))
			transport.queueFrame(t, session.sessionID, idleEvt, 1, nil)
			transport.queueFrame(t, session.sessionID, valueEvt, 2,
				buildStringPayload("nil"))
			transport.queueFrame(t, session.sessionID, idleEvt, 2, nil)

			manager := NewManager(ManagerConfig{})
			manager.conn = &managedConnection{
				session: session,
				info: &baseproto.HelloResponse{
					Board:   "mock-board",
					Version: "0.1.0-test",
				},
			}

			value, err := tc.run(manager)
			if err != nil {
				t.Fatalf("run: %v", err)
			}
			if value != "nil" {
				t.Fatalf("value = %q, want nil", value)
			}
			if len(transport.writes) != 2 {
				t.Fatalf("write count = %d, want 2", len(transport.writes))
			}

			directHeader, directPayload := decodeWriteFrame(t, transport.writes[0])
			if directHeader.MessageType != tc.directReq {
				t.Fatalf("direct type = %d, want %d", directHeader.MessageType,
					tc.directReq)
			}
			if tc.directReq == saveReq || tc.directReq == restoreReq ||
				tc.directReq == wipeReq {
				if len(directPayload) != 0 {
					t.Fatalf("direct payload len = %d, want 0", len(directPayload))
				}
			}

			evalHeader, evalPayload := decodeWriteFrame(t, transport.writes[1])
			if evalHeader.MessageType != evalReq {
				t.Fatalf("fallback type = %d, want %d", evalHeader.MessageType, evalReq)
			}
			source, err := parseStringPayload(evalPayload)
			if err != nil {
				t.Fatalf("parse fallback payload: %v", err)
			}
			if source != tc.wantEvalSource {
				t.Fatalf("fallback source = %q, want %q", source, tc.wantEvalSource)
			}
		})
	}
}
