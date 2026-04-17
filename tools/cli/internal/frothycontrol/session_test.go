package frothycontrol

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"sync"
	"testing"
	"time"

	baseproto "github.com/nikokozak/frothy/tools/cli/internal/protocol"
	"github.com/nikokozak/frothy/tools/cli/internal/serial"
)

type stubTransport struct {
	mu          sync.Mutex
	readBuf     bytes.Buffer
	writes      [][]byte
	onWrite     func([]byte)
	readTimeout time.Duration
}

type blockingTransport struct {
	mu          sync.Mutex
	writes      [][]byte
	readTimeout time.Duration
	closed      chan struct{}
	readStarted chan struct{}
	readOnce    sync.Once
	closeOnce   sync.Once
}

type closeCountingTransport struct {
	*stubTransport
	closeCount int
	closeErr   error
}

func (t *closeCountingTransport) Close() error {
	t.closeCount++
	return t.closeErr
}

func newBlockingTransport() *blockingTransport {
	return &blockingTransport{
		closed:      make(chan struct{}),
		readStarted: make(chan struct{}),
	}
}

func (t *blockingTransport) Read([]byte) (int, error) {
	t.readOnce.Do(func() {
		close(t.readStarted)
	})
	<-t.closed
	return 0, io.EOF
}

func (t *blockingTransport) Write(data []byte) error {
	writeCopy := append([]byte(nil), data...)
	t.mu.Lock()
	t.writes = append(t.writes, writeCopy)
	t.mu.Unlock()
	return nil
}

func (t *blockingTransport) Close() error {
	t.closeOnce.Do(func() {
		close(t.closed)
	})
	return nil
}

func (t *blockingTransport) Path() string { return "blocking" }

func (t *blockingTransport) SetReadTimeout(timeout time.Duration) error {
	t.mu.Lock()
	t.readTimeout = timeout
	t.mu.Unlock()
	return nil
}

func (t *blockingTransport) ResetInputBuffer() {}

func (t *blockingTransport) Drain(time.Duration) {}

func (t *stubTransport) Read(buf []byte) (int, error) {
	t.mu.Lock()
	if t.readBuf.Len() > 0 {
		n, err := t.readBuf.Read(buf)
		t.mu.Unlock()
		return n, err
	}
	timeout := t.readTimeout
	t.mu.Unlock()

	if timeout > 0 {
		time.Sleep(timeout)
	}

	t.mu.Lock()
	defer t.mu.Unlock()
	if t.readBuf.Len() == 0 {
		return 0, nil
	}
	return t.readBuf.Read(buf)
}

func (t *stubTransport) Write(data []byte) error {
	writeCopy := append([]byte(nil), data...)
	t.mu.Lock()
	t.writes = append(t.writes, writeCopy)
	onWrite := t.onWrite
	t.mu.Unlock()
	if onWrite != nil {
		onWrite(writeCopy)
	}
	return nil
}

func (t *stubTransport) Close() error { return nil }

func (t *stubTransport) Path() string { return "stub" }

func (t *stubTransport) SetReadTimeout(timeout time.Duration) error {
	t.mu.Lock()
	t.readTimeout = timeout
	t.mu.Unlock()
	return nil
}

func (t *stubTransport) ResetInputBuffer() {}

func (t *stubTransport) Drain(time.Duration) {}

func (t *stubTransport) queueRaw(data string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.readBuf.WriteString(data)
}

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

func buildHelloPayload(board string) []byte {
	payload := []byte{32}
	payload = binary.LittleEndian.AppendUint16(payload, 256)
	payload = binary.LittleEndian.AppendUint32(payload, 8192)
	payload = binary.LittleEndian.AppendUint32(payload, 2048)
	payload = binary.LittleEndian.AppendUint16(payload, 200)
	payload = append(payload, 0)
	payload = append(payload, buildStringPayload("0.1.0-test")...)
	payload = append(payload, buildStringPayload(board)...)
	payload = append(payload, 0)
	return payload
}

func newReadyControlTransport(t *testing.T, board string) *closeCountingTransport {
	t.Helper()

	stub := &stubTransport{}
	transport := &closeCountingTransport{stubTransport: stub}
	stub.queueRaw("frothy> ")
	stub.onWrite = func(data []byte) {
		if bytes.Equal(data, []byte(".control\n")) {
			stub.queueRaw("control: ready")
			return
		}
		if len(data) > 0 && data[0] == 0x00 {
			header, _ := decodeWriteFrame(t, data)
			if header.MessageType == helloReq {
				stub.queueFrame(t, header.SessionID, helloEvt, header.Seq,
					buildHelloPayload(board))
				stub.queueFrame(t, header.SessionID, idleEvt, header.Seq, nil)
			}
		}
	}
	return transport
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

func TestManagerDisconnectClosesTransportDuringRunningEval(t *testing.T) {
	transport := newBlockingTransport()
	session := NewSession(transport)
	session.sessionID = 0x55
	session.nextSeq = 1

	manager := NewManager(ManagerConfig{})
	manager.conn = &managedConnection{
		transport: transport,
		session:   session,
		info: &baseproto.HelloResponse{
			Board:   "mock-board",
			Version: "0.1.0-test",
		},
	}

	evalDone := make(chan error, 1)
	go func() {
		_, err := manager.Eval("while true [ nil ]", nil)
		evalDone <- err
	}()

	select {
	case <-transport.readStarted:
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("eval did not start reading")
	}

	disconnectDone := make(chan error, 1)
	go func() {
		disconnectDone <- manager.Disconnect()
	}()

	select {
	case err := <-disconnectDone:
		if err != nil {
			t.Fatalf("Disconnect() error = %v", err)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("Disconnect() blocked behind running eval")
	}

	select {
	case err := <-evalDone:
		if err == nil {
			t.Fatalf("Eval() error = nil, want closed transport error")
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("Eval() did not unblock after disconnect")
	}

	if manager.IsConnected() {
		t.Fatalf("manager still connected after disconnect")
	}
}

func TestManagerDisconnectWaitsForConnectOperation(t *testing.T) {
	manager := NewManager(ManagerConfig{})
	manager.opMu.Lock()
	manager.setOperation(managerOpConnect)

	done := make(chan error, 1)
	go func() {
		done <- manager.Disconnect()
	}()

	select {
	case err := <-done:
		t.Fatalf("Disconnect returned during connect operation: %v", err)
	case <-time.After(50 * time.Millisecond):
	}

	manager.setOperation(managerOpNone)
	manager.opMu.Unlock()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("Disconnect error = %v", err)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("Disconnect did not return after connect operation ended")
	}
}

func TestManagerDisconnectCancelsPendingConnect(t *testing.T) {
	manager := NewManager(ManagerConfig{})
	manager.opMu.Lock()
	manager.setOperation(managerOpRequest)

	openCalls := 0
	originalOpenSerialTransport := openSerialTransport
	openSerialTransport = func(path string) (serial.Transport, error) {
		openCalls += 1
		return nil, errors.New("open should not be called")
	}
	defer func() {
		openSerialTransport = originalOpenSerialTransport
	}()

	connectDone := make(chan error, 1)
	go func() {
		_, err := manager.Connect("/dev/new")
		connectDone <- err
	}()

	select {
	case err := <-connectDone:
		t.Fatalf("Connect returned before pending request released: %v", err)
	case <-time.After(50 * time.Millisecond):
	}

	if err := manager.Disconnect(); err != nil {
		t.Fatalf("Disconnect error = %v", err)
	}
	manager.setOperation(managerOpNone)
	manager.opMu.Unlock()

	select {
	case err := <-connectDone:
		if !errors.Is(err, ErrNotConnected) {
			t.Fatalf("Connect error = %v, want ErrNotConnected", err)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("Connect did not return after pending request released")
	}
	if openCalls != 0 {
		t.Fatalf("open calls = %d, want 0 for cancelled pending connect", openCalls)
	}
}

func TestManagerPortSwitchClosesNewConnectionWhenOldCloseFails(t *testing.T) {
	oldCloseErr := errors.New("old close failed")
	oldTransport := &closeCountingTransport{
		stubTransport: &stubTransport{},
		closeErr:      oldCloseErr,
	}
	manager := NewManager(ManagerConfig{})
	manager.conn = &managedConnection{
		transport: oldTransport,
		port:      "/dev/old",
		info: &baseproto.HelloResponse{
			Board:   "old-board",
			Version: "0.1.0-test",
		},
	}

	var newTransport *closeCountingTransport
	originalOpenSerialTransport := openSerialTransport
	openSerialTransport = func(path string) (serial.Transport, error) {
		if path != "/dev/new" {
			t.Fatalf("open path = %q, want /dev/new", path)
		}
		newTransport = newReadyControlTransport(t, "new-board")
		return newTransport, nil
	}
	defer func() {
		openSerialTransport = originalOpenSerialTransport
	}()

	if _, err := manager.Connect("/dev/new"); !errors.Is(err, oldCloseErr) {
		t.Fatalf("Connect error = %v, want old close error", err)
	}
	if oldTransport.closeCount != 1 {
		t.Fatalf("old close count = %d, want 1", oldTransport.closeCount)
	}
	if newTransport == nil {
		t.Fatalf("new transport was not opened")
	}
	if newTransport.closeCount != 1 {
		t.Fatalf("new close count = %d, want 1", newTransport.closeCount)
	}
	if got := manager.connection(); got != nil {
		t.Fatalf("manager connection = %#v, want disconnected after close failure", got)
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
			wantEvalSource: "save:",
		},
		{
			name: "restore",
			run: func(m *Manager) (string, error) {
				return m.Restore(nil)
			},
			directReq:      restoreReq,
			wantEvalSource: "restore:",
		},
		{
			name: "wipe",
			run: func(m *Manager) (string, error) {
				return m.Wipe(nil)
			},
			directReq:      wipeReq,
			wantEvalSource: "dangerous.wipe:",
		},
		{
			name: "core",
			run: func(m *Manager) (string, error) {
				return m.Core("save", nil)
			},
			directReq:      coreReq,
			wantEvalSource: "core: @save",
		},
		{
			name: "slot_info",
			run: func(m *Manager) (string, error) {
				return m.SlotInfo("save", nil)
			},
			directReq:      slotInfoReq,
			wantEvalSource: "slotInfo: @save",
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

func TestSessionAcquirePromptRecoversAfterCtrlC(t *testing.T) {
	transport := &stubTransport{}
	transport.onWrite = func(data []byte) {
		if bytes.Equal(data, []byte{0x03}) {
			transport.readBuf.WriteString("boot noise\nfrothy> ")
		}
	}

	session := NewSession(transport)
	if err := session.AcquirePrompt(1200 * time.Millisecond); err != nil {
		t.Fatalf("AcquirePrompt() error = %v", err)
	}

	if len(transport.writes) != 1 {
		t.Fatalf("writes = %q, want ctrl-c-only recovery", transport.writes)
	}
	if !bytes.Equal(transport.writes[0], []byte{0x03}) {
		t.Fatalf("first recovery write = %q, want ctrl-c", transport.writes[0])
	}
}

func TestSessionAcquirePromptRecoversAfterCtrlCNewlineBurst(t *testing.T) {
	transport := &stubTransport{}
	transport.onWrite = func(data []byte) {
		if bytes.Equal(data, []byte{0x03, '\n'}) {
			transport.readBuf.WriteString("frothy> ")
		}
	}

	session := NewSession(transport)
	if err := session.AcquirePrompt(2500 * time.Millisecond); err != nil {
		t.Fatalf("AcquirePrompt() error = %v", err)
	}

	if len(transport.writes) != 3 {
		t.Fatalf("writes = %q, want recovery burst", transport.writes)
	}
	if !bytes.Equal(transport.writes[0], []byte{0x03}) {
		t.Fatalf("first recovery write = %q, want ctrl-c", transport.writes[0])
	}
	if !bytes.Equal(transport.writes[1], []byte{'\n'}) {
		t.Fatalf("second recovery write = %q, want newline", transport.writes[1])
	}
	lastWrite := transport.writes[len(transport.writes)-1]
	if !bytes.Equal(lastWrite, []byte{0x03, '\n'}) {
		t.Fatalf("last recovery write = %q, want ctrl-c/newline burst", lastWrite)
	}
}

func TestSessionAcquirePromptWaitsForSlowPromptBeforeRecovery(t *testing.T) {
	transport := &stubTransport{}
	go func() {
		time.Sleep(700 * time.Millisecond)
		transport.queueRaw("boot noise\nfrothy> ")
	}()

	session := NewSession(transport)
	if err := session.AcquirePrompt(1500 * time.Millisecond); err != nil {
		t.Fatalf("AcquirePrompt() error = %v", err)
	}

	if len(transport.writes) != 0 {
		t.Fatalf("writes = %q, want passive wait without recovery bytes", transport.writes)
	}
}
