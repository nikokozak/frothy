package frothycontrol

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"sync"
	"testing"
	"time"

	baseproto "github.com/nikokozak/frothy/tools/cli/internal/protocol"
)

type fakeBackend struct {
	mu             sync.Mutex
	connected      bool
	device         *DeviceInfo
	connectErr     error
	connectStarted chan struct{}
	unblockConnect chan struct{}
	evalValue      string
	evalOutput     []byte
	evalErr        error
	resetResult    *baseproto.ResetResponse
	resetErr       error
	wordsResult    []string
	wordsErr       error
	seeResult      *SeeResult
	seeErr         error
	interruptErr   error
	disconnectErr  error
	disconnectHits int
	blockEval      chan struct{}
	coreValue      string
	coreOutput     []byte
	coreErr        error
	slotInfoValue  string
	slotInfoOutput []byte
	slotInfoErr    error
}

type overlappingConnectBackend struct {
	mu             sync.Mutex
	connectCalls   int
	disconnectHits int
	firstStarted   chan struct{}
	releaseFirst   chan struct{}
}

func (b *fakeBackend) IsConnected() bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.connected
}

func (b *fakeBackend) Connect(string) (*DeviceInfo, error) {
	b.mu.Lock()
	started := b.connectStarted
	b.connectStarted = nil
	unblock := b.unblockConnect
	b.mu.Unlock()

	if started != nil {
		close(started)
	}
	if unblock != nil {
		<-unblock
	}

	b.mu.Lock()
	defer b.mu.Unlock()
	if b.connectErr != nil {
		b.connected = false
		return nil, b.connectErr
	}
	b.connected = true
	return b.device, nil
}

func (b *fakeBackend) Disconnect() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.connected = false
	b.disconnectHits++
	return b.disconnectErr
}

func (b *fakeBackend) Eval(source string, onOutput func([]byte)) (string, error) {
	if len(b.evalOutput) > 0 {
		onOutput(b.evalOutput)
	}
	b.mu.Lock()
	blockEval := b.blockEval
	b.mu.Unlock()
	if blockEval != nil {
		<-blockEval
	}
	return b.evalValue + source[:0], b.evalErr
}

func (b *fakeBackend) Reset() (*baseproto.ResetResponse, error) {
	return b.resetResult, b.resetErr
}

func (b *fakeBackend) Interrupt() error {
	b.mu.Lock()
	blockEval := b.blockEval
	b.blockEval = nil
	b.mu.Unlock()
	if blockEval != nil {
		close(blockEval)
	}
	return b.interruptErr
}

func (b *fakeBackend) Words() ([]string, error) {
	return b.wordsResult, b.wordsErr
}

func (b *fakeBackend) See(string) (*SeeResult, error) {
	return b.seeResult, b.seeErr
}

func (b *fakeBackend) Save(onOutput func([]byte)) (string, error) {
	return b.Eval("save:", onOutput)
}

func (b *fakeBackend) Restore(onOutput func([]byte)) (string, error) {
	return b.Eval("restore:", onOutput)
}

func (b *fakeBackend) Wipe(onOutput func([]byte)) (string, error) {
	return b.Eval("dangerous.wipe:", onOutput)
}

func (b *fakeBackend) Core(name string, onOutput func([]byte)) (string, error) {
	_ = name
	if len(b.coreOutput) > 0 {
		onOutput(b.coreOutput)
	}
	if b.coreValue == "" && b.coreErr == nil {
		return "nil", nil
	}
	return b.coreValue, b.coreErr
}

func (b *fakeBackend) SlotInfo(name string, onOutput func([]byte)) (string, error) {
	_ = name
	if len(b.slotInfoOutput) > 0 {
		onOutput(b.slotInfoOutput)
	}
	if b.slotInfoValue == "" && b.slotInfoErr == nil {
		return "nil", nil
	}
	return b.slotInfoValue, b.slotInfoErr
}

func (b *overlappingConnectBackend) IsConnected() bool { return true }

func (b *overlappingConnectBackend) Connect(string) (*DeviceInfo, error) {
	b.mu.Lock()
	b.connectCalls++
	call := b.connectCalls
	b.mu.Unlock()

	if call == 1 {
		close(b.firstStarted)
		<-b.releaseFirst
	}

	return &DeviceInfo{
		Port:     "/dev/cu.mock",
		Board:    "mock-board",
		Version:  "0.1.0-test",
		CellBits: 32,
	}, nil
}

func (b *overlappingConnectBackend) Disconnect() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.disconnectHits++
	return nil
}

func (b *overlappingConnectBackend) Eval(string, func([]byte)) (string, error) {
	return "nil", nil
}

func (b *overlappingConnectBackend) Reset() (*baseproto.ResetResponse, error) {
	return nil, nil
}

func (b *overlappingConnectBackend) Interrupt() error { return nil }

func (b *overlappingConnectBackend) Words() ([]string, error) { return nil, nil }

func (b *overlappingConnectBackend) See(string) (*SeeResult, error) {
	return nil, nil
}

func (b *overlappingConnectBackend) Save(func([]byte)) (string, error) {
	return "nil", nil
}

func (b *overlappingConnectBackend) Restore(func([]byte)) (string, error) {
	return "nil", nil
}

func (b *overlappingConnectBackend) Wipe(func([]byte)) (string, error) {
	return "nil", nil
}

func (b *overlappingConnectBackend) Core(string, func([]byte)) (string, error) {
	return "nil", nil
}

func (b *overlappingConnectBackend) SlotInfo(string, func([]byte)) (string, error) {
	return "nil", nil
}

type sessionHarness struct {
	inWriter *io.PipeWriter
	messages chan map[string]any
	done     chan error
}

func startHarness(t *testing.T, backend HelperBackend) *sessionHarness {
	t.Helper()

	inReader, inWriter := io.Pipe()
	outReader, outWriter := io.Pipe()
	done := make(chan error, 1)

	go func() {
		done <- RunControlSessionServer(backend, inReader, outWriter)
		_ = outWriter.Close()
	}()

	messages := make(chan map[string]any, 32)
	go func() {
		scanner := bufio.NewScanner(outReader)
		for scanner.Scan() {
			var message map[string]any
			if err := json.Unmarshal(scanner.Bytes(), &message); err == nil {
				messages <- message
			}
		}
		close(messages)
	}()

	return &sessionHarness{
		inWriter: inWriter,
		messages: messages,
		done:     done,
	}
}

func (h *sessionHarness) send(t *testing.T, request controlSessionRequest) {
	t.Helper()

	line, err := json.Marshal(request)
	if err != nil {
		t.Fatalf("marshal request: %v", err)
	}
	if _, err := h.inWriter.Write(append(line, '\n')); err != nil {
		t.Fatalf("write request: %v", err)
	}
}

func (h *sessionHarness) next(t *testing.T) map[string]any {
	t.Helper()

	message, ok := <-h.messages
	if !ok {
		t.Fatal("message channel closed")
	}
	return message
}

func (h *sessionHarness) nextWithin(t *testing.T,
	timeout time.Duration) map[string]any {
	t.Helper()

	select {
	case message, ok := <-h.messages:
		if !ok {
			t.Fatal("message channel closed")
		}
		return message
	case <-time.After(timeout):
		t.Fatalf("timed out waiting for session message")
		return nil
	}
}

func (h *sessionHarness) close(t *testing.T) {
	t.Helper()

	_ = h.inWriter.Close()
	if err := <-h.done; err != nil {
		t.Fatalf("server returned error: %v", err)
	}
}

func TestControlSessionServerConnectEvalAndResetUnavailable(t *testing.T) {
	backend := &fakeBackend{
		device: &DeviceInfo{
			Port:     "/dev/cu.mock",
			Board:    "mock-board",
			Version:  "0.1.0-test",
			CellBits: 32,
		},
		evalValue:  "2",
		evalOutput: []byte("hello\n"),
		resetErr:   ErrResetUnavailable,
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "connect"})
	if event := harness.next(t); event["event"] != "connected" {
		t.Fatalf("first event = %v, want connected", event)
	}
	if response := harness.next(t); response["ok"] != true {
		t.Fatalf("connect response = %v", response)
	}

	harness.send(t, controlSessionRequest{ID: 2, Command: "eval", Source: "1 + 1"})
	if event := harness.next(t); event["event"] != "output" {
		t.Fatalf("eval output event = %v", event)
	}
	if event := harness.next(t); event["event"] != "value" {
		t.Fatalf("eval value event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("eval idle event = %v", event)
	}
	if response := harness.next(t); response["ok"] != true {
		t.Fatalf("eval response = %v", response)
	}

	harness.send(t, controlSessionRequest{ID: 3, Command: "reset"})
	if event := harness.next(t); event["event"] != "error" {
		t.Fatalf("reset error event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("reset idle event = %v", event)
	}
	response := harness.next(t)
	if response["ok"] != false {
		t.Fatalf("reset response = %v", response)
	}
	errorValue, _ := response["error"].(map[string]any)
	if errorValue["code"] != "reset_unavailable" {
		t.Fatalf("reset error = %v", errorValue)
	}
}

func TestControlSessionServerInterruptsRunningEval(t *testing.T) {
	backend := &fakeBackend{
		blockEval:  make(chan struct{}),
		evalOutput: []byte("running\n"),
		evalErr:    ErrInterrupted,
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "eval", Source: "loop"})
	if event := harness.next(t); event["event"] != "output" {
		t.Fatalf("eval output event = %v", event)
	}

	harness.send(t, controlSessionRequest{ID: 2, Command: "interrupt"})
	sawInterruptResponse := false
	sawInterruptedEvent := false
	sawIdleEvent := false
	sawEvalResponse := false
	for i := 0; i < 4; i++ {
		message := harness.next(t)
		if message["type"] == "response" {
			switch message["id"] {
			case float64(2):
				if message["ok"] != true {
					t.Fatalf("interrupt response = %v", message)
				}
				sawInterruptResponse = true
			case float64(1):
				if message["ok"] != false {
					t.Fatalf("eval interrupted response = %v", message)
				}
				sawEvalResponse = true
			default:
				t.Fatalf("unexpected response = %v", message)
			}
			continue
		}

		switch message["event"] {
		case "interrupted":
			sawInterruptedEvent = true
		case "idle":
			sawIdleEvent = true
		default:
			t.Fatalf("unexpected event = %v", message)
		}
	}
	if !sawInterruptResponse || !sawInterruptedEvent || !sawIdleEvent || !sawEvalResponse {
		t.Fatalf("missing interrupt cycle: response=%v interrupted=%v idle=%v eval=%v",
			sawInterruptResponse, sawInterruptedEvent, sawIdleEvent, sawEvalResponse)
	}
}

func TestControlSessionServerDisconnectPreemptsBlockedConnect(t *testing.T) {
	backend := &fakeBackend{
		device: &DeviceInfo{
			Port:     "/dev/cu.mock",
			Board:    "mock-board",
			Version:  "0.1.0-test",
			CellBits: 32,
		},
		connectStarted: make(chan struct{}),
		unblockConnect: make(chan struct{}),
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	connectStarted := backend.connectStarted
	harness.send(t, controlSessionRequest{ID: 1, Command: "connect"})
	select {
	case <-connectStarted:
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("connect did not start")
	}

	harness.send(t, controlSessionRequest{ID: 2, Command: "disconnect"})
	sawDisconnectEvent := false
	sawDisconnectResponse := false
	for !sawDisconnectEvent || !sawDisconnectResponse {
		message := harness.nextWithin(t, 500*time.Millisecond)
		if message["event"] == "connected" {
			t.Fatalf("blocked connect published connected before disconnect: %v", message)
		}
		if message["event"] == "disconnected" {
			sawDisconnectEvent = true
			continue
		}
		if message["type"] == "response" && message["id"] == float64(2) {
			if message["ok"] != true {
				t.Fatalf("disconnect response = %v", message)
			}
			sawDisconnectResponse = true
			continue
		}
		t.Fatalf("unexpected message before blocked connect release: %v", message)
	}

	close(backend.unblockConnect)
	message := harness.nextWithin(t, 500*time.Millisecond)
	if message["type"] != "response" || message["id"] != float64(1) ||
		message["ok"] != false {
		t.Fatalf("superseded connect response = %v", message)
	}
}

func TestControlSessionServerConnectFailureReportsDisconnected(t *testing.T) {
	backend := &fakeBackend{
		connected:  true,
		connectErr: errors.New("reconnect failed"),
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "connect"})
	if event := harness.nextWithin(t, 500*time.Millisecond); event["event"] != "disconnected" {
		t.Fatalf("connect failure event = %v, want disconnected", event)
	}
	response := harness.nextWithin(t, 500*time.Millisecond)
	if response["type"] != "response" || response["id"] != float64(1) ||
		response["ok"] != false {
		t.Fatalf("connect failure response = %v", response)
	}
}

func TestControlSessionServerSupersededConnectDoesNotDisconnectNewerConnect(t *testing.T) {
	backend := &overlappingConnectBackend{
		firstStarted: make(chan struct{}),
		releaseFirst: make(chan struct{}),
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "connect"})
	select {
	case <-backend.firstStarted:
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("first connect did not start")
	}

	harness.send(t, controlSessionRequest{ID: 2, Command: "connect"})
	if event := harness.nextWithin(t, 500*time.Millisecond); event["event"] != "connected" ||
		event["request_id"] != float64(2) {
		t.Fatalf("newer connect event = %v", event)
	}
	if response := harness.nextWithin(t, 500*time.Millisecond); response["type"] != "response" ||
		response["id"] != float64(2) || response["ok"] != true {
		t.Fatalf("newer connect response = %v", response)
	}

	close(backend.releaseFirst)
	if response := harness.nextWithin(t, 500*time.Millisecond); response["type"] != "response" ||
		response["id"] != float64(1) || response["ok"] != false {
		t.Fatalf("superseded connect response = %v", response)
	}
	if backend.disconnectHits != 0 {
		t.Fatalf("disconnect hits = %d, want 0", backend.disconnectHits)
	}
}

func TestControlSessionServerDisconnectsOnEOF(t *testing.T) {
	backend := &fakeBackend{}
	harness := startHarness(t, backend)

	harness.close(t)
	if backend.disconnectHits != 1 {
		t.Fatalf("disconnect count = %d, want 1", backend.disconnectHits)
	}
}

func TestControlSessionServerReportsStructuredErrors(t *testing.T) {
	backend := &fakeBackend{
		seeErr: &ControlError{
			Phase:  phaseInspect,
			Code:   7,
			Detail: "see failed",
		},
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "see", Name: "missing"})
	if event := harness.next(t); event["event"] != "error" {
		t.Fatalf("error event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("idle event = %v", event)
	}
	response := harness.next(t)
	if response["ok"] != false {
		t.Fatalf("see response = %v", response)
	}
	errorPayload, _ := response["error"].(map[string]any)
	if errorPayload["code"] != "control_error" {
		t.Fatalf("error payload = %v", errorPayload)
	}
}

func TestControlSessionServerCorePreservesOutputCycle(t *testing.T) {
	backend := &fakeBackend{
		coreOutput: []byte("<native save/0>\n"),
		coreValue:  "nil",
	}
	harness := startHarness(t, backend)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "core", Name: "save"})
	if event := harness.next(t); event["event"] != "output" {
		t.Fatalf("core output event = %v", event)
	}
	if event := harness.next(t); event["event"] != "value" {
		t.Fatalf("core value event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("core idle event = %v", event)
	}
	response := harness.next(t)
	if response["ok"] != true {
		t.Fatalf("core response = %v", response)
	}
}

func TestHelperErrorFromConnectSelectionError(t *testing.T) {
	err := helperErrorFrom(&ConnectSelectionError{
		Code: "multiple_devices",
		Candidates: []ConnectCandidate{{
			Port: "/dev/cu.mock",
		}},
	})

	payload, marshalErr := json.Marshal(err)
	if marshalErr != nil {
		t.Fatalf("marshal helper error: %v", marshalErr)
	}
	if !bytes.Contains(payload, []byte("multiple_devices")) {
		t.Fatalf("helper error payload = %s", payload)
	}
}
