package frothycontrol

import (
	"bytes"
	"errors"
	"fmt"
	"strings"
	"testing"

	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
)

const rawReplLineLimit = 1024

func TestLocalRuntimeSessionSupportsResetAndChunkedInspect(t *testing.T) {
	if baseproto.MaxPayload < 32 {
		t.Skip("payload too small for chunked inspect integration")
	}

	runtime, session := openLocalRuntimeRaw(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	bigText := strings.Repeat("a", baseproto.MaxPayload)
	if !rawSeedSourceFits(fmt.Sprintf("bigText = %q", bigText)) {
		t.Skip("payload too large for raw REPL chunked SEE seeding")
	}
	seedRawDefinition(t, session, fmt.Sprintf("bigText = %q", bigText))
	enterLocalRuntimeControl(t, runtime, session)

	for i := 0; i < 32; i++ {
		source := fmt.Sprintf("chunk.word.%02d = %d", i, i)
		if _, err := session.Eval(source, controlCommandTimeout, nil); err != nil {
			t.Fatalf("seed %s: %v", source, err)
		}
	}

	words, err := session.Words(controlCommandTimeout)
	if err != nil {
		t.Fatalf("Words: %v", err)
	}
	if !contains(words, "chunk.word.00") || !contains(words, "chunk.word.31") {
		t.Fatalf("Words missing chunked bindings: %v", words)
	}

	view, err := session.See("bigText", controlCommandTimeout)
	if err != nil {
		t.Fatalf("See bigText: %v", err)
	}
	if view.Rendered != fmt.Sprintf("%q", bigText) {
		t.Fatalf("See rendered = %q", view.Rendered)
	}

	reset, err := session.Reset(controlCommandTimeout)
	if err != nil {
		t.Fatalf("Reset: %v", err)
	}
	if reset.Status != 0 || reset.HeapOverlayUsed != 0 || reset.SlotOverlayCount != 0 {
		t.Fatalf("Reset result = %+v", reset)
	}

	if _, err := session.Eval("post.reset = 99", controlCommandTimeout, nil); err != nil {
		t.Fatalf("post-reset eval: %v", err)
	}
	value, err := session.Eval("post.reset", controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("post-reset lookup: %v", err)
	}
	if value != "99" {
		t.Fatalf("post-reset value = %q", value)
	}

	_, err = session.See("bigText", controlCommandTimeout)
	var controlErr *ControlError
	if !errors.As(err, &controlErr) {
		t.Fatalf("See after reset error = %v, want ControlError", err)
	}
	if controlErr.Phase != phaseInspect || controlErr.Detail != "see failed" {
		t.Fatalf("See after reset control error = %+v", controlErr)
	}
}

func TestLocalRuntimeSessionMalformedRequestsReturnStructuredErrors(t *testing.T) {
	runtime, session := openLocalRuntimeSession(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	type malformedCase struct {
		name       string
		msgType    byte
		seq        uint16
		payload    []byte
		wantCode   uint16
		wantDetail string
	}

	cases := []malformedCase{
		{
			name:       "bad words payload",
			msgType:    wordsReq,
			seq:        1,
			payload:    []byte{0x01},
			wantCode:   108,
			wantDetail: "bad WORDS payload",
		},
		{
			name:       "truncated see payload",
			msgType:    seeReq,
			seq:        2,
			payload:    []byte{0x05, 0x00, 'a'},
			wantCode:   251,
			wantDetail: "bad SEE payload",
		},
		{
			name:       "see payload trailing bytes",
			msgType:    seeReq,
			seq:        3,
			payload:    append(buildStringPayload("save"), 0x00),
			wantCode:   108,
			wantDetail: "bad SEE payload",
		},
		{
			name:       "unknown request",
			msgType:    0x42,
			seq:        4,
			wantCode:   256,
			wantDetail: "unknown request",
		},
		{
			name:       "bad save payload",
			msgType:    saveReq,
			seq:        5,
			payload:    []byte{0x01},
			wantCode:   108,
			wantDetail: "bad SAVE payload",
		},
		{
			name:       "bad restore payload",
			msgType:    restoreReq,
			seq:        6,
			payload:    []byte{0x01},
			wantCode:   108,
			wantDetail: "bad RESTORE payload",
		},
		{
			name:       "bad wipe payload",
			msgType:    wipeReq,
			seq:        7,
			payload:    []byte{0x01},
			wantCode:   108,
			wantDetail: "bad WIPE payload",
		},
		{
			name:       "truncated core payload",
			msgType:    coreReq,
			seq:        8,
			payload:    []byte{0x05, 0x00, 'a'},
			wantCode:   251,
			wantDetail: "bad CORE payload",
		},
		{
			name:       "slot info payload trailing bytes",
			msgType:    slotInfoReq,
			seq:        9,
			payload:    append(buildStringPayload("save"), 0x00),
			wantCode:   108,
			wantDetail: "bad SLOT_INFO payload",
		},
		{
			name:       "unexpected sequence",
			msgType:    wordsReq,
			seq:        11,
			wantCode:   108,
			wantDetail: "unexpected sequence",
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			outcome, err := session.runRequest(tc.msgType, tc.seq, tc.payload,
				controlCommandTimeout, nil)
			if err != nil {
				t.Fatalf("runRequest: %v", err)
			}
			if outcome.errEvent == nil {
				t.Fatalf("outcome = %+v, want error event", outcome)
			}
			if outcome.errEvent.Code != tc.wantCode ||
				outcome.errEvent.Detail != tc.wantDetail {
				t.Fatalf("error event = %+v, want code=%d detail=%q",
					outcome.errEvent, tc.wantCode, tc.wantDetail)
			}
		})
	}

	outcome, err := session.runRequest(wordsReq, 10, nil, controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("recovery WORDS: %v", err)
	}
	if outcome.errEvent != nil {
		t.Fatalf("recovery WORDS error = %+v", outcome.errEvent)
	}
	names, err := parseWordsPayloads(outcome.valuePayloads)
	if err != nil {
		t.Fatalf("parse recovery WORDS: %v", err)
	}
	if len(names) == 0 {
		t.Fatalf("recovery WORDS returned no bindings")
	}
	session.nextSeq = 11
}

func TestLocalRuntimeSessionMultilineInterruptEntersControl(t *testing.T) {
	runtime, session := openLocalRuntimeRaw(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	if err := session.writeBytes([]byte("control.check = fn(x) {\n")); err != nil {
		t.Fatalf("write multiline source: %v", err)
	}
	if _, err := session.waitForRaw([]byte(".. "), rawPromptTimeout); err != nil {
		t.Fatalf("wait for continuation prompt: %v", err)
	}
	if err := session.Interrupt(); err != nil {
		t.Fatalf("interrupt multiline input: %v", err)
	}
	if _, err := session.waitForRaw([]byte("frothy> "), rawPromptTimeout); err != nil {
		t.Fatalf("wait for prompt after multiline interrupt: %v", err)
	}

	enterLocalRuntimeControl(t, runtime, session)

	value, err := session.Eval("1 + 1", controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("post-interrupt eval: %v", err)
	}
	if value != "2" {
		t.Fatalf("post-interrupt value = %q, want 2", value)
	}

	if err := session.Detach(controlCommandTimeout); err != nil {
		t.Fatalf("Detach: %v", err)
	}
	session.sessionID = 0
	session.nextSeq = 0
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		t.Fatalf("prompt after detach: %v", err)
	}
}

func TestLocalRuntimeSessionIdleControlInterruptReattaches(t *testing.T) {
	runtime, session := openLocalRuntimeSession(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	if err := session.Interrupt(); err != nil {
		t.Fatalf("interrupt idle control session: %v", err)
	}
	if _, err := session.waitForRaw([]byte("frothy> "), rawPromptTimeout); err != nil {
		t.Fatalf("wait for prompt after idle control interrupt: %v", err)
	}
	session.sessionID = 0
	session.nextSeq = 0

	enterLocalRuntimeControl(t, runtime, session)

	value, err := session.Eval("2 + 2", controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("post-reattach eval: %v", err)
	}
	if value != "4" {
		t.Fatalf("post-reattach value = %q, want 4", value)
	}

	if err := session.Detach(controlCommandTimeout); err != nil {
		t.Fatalf("Detach: %v", err)
	}
	session.sessionID = 0
	session.nextSeq = 0
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		t.Fatalf("prompt after detach: %v", err)
	}
}

func TestLocalRuntimeSessionOversizedInspectFailsWithoutValueEvents(t *testing.T) {
	if baseproto.MaxPayload < 4 {
		t.Skip("payload too small for oversized inspect regression")
	}
	if !rawSeedSourceFits(strings.Repeat("a", baseproto.MaxPayload-3) + " = 1") {
		t.Skip("payload too large for raw REPL seeding")
	}

	runtime, session := openLocalRuntimeRaw(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	longName := strings.Repeat("a", baseproto.MaxPayload-3)
	seedRawDefinition(t, session, fmt.Sprintf("%s = 1", longName))
	enterLocalRuntimeControl(t, runtime, session)

	wordsOutcome, err := session.runRequest(wordsReq, 1, nil, controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("WORDS runRequest: %v", err)
	}
	if wordsOutcome.errEvent == nil {
		t.Fatalf("WORDS outcome = %+v, want error event", wordsOutcome)
	}
	if wordsOutcome.errEvent.Detail != "words failed" {
		t.Fatalf("WORDS error detail = %q", wordsOutcome.errEvent.Detail)
	}
	if len(wordsOutcome.valuePayloads) != 0 {
		t.Fatalf("WORDS value payloads = %d, want 0", len(wordsOutcome.valuePayloads))
	}

	seeOutcome, err := session.runRequest(seeReq, 2, buildStringPayload(longName),
		controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("SEE runRequest: %v", err)
	}
	if seeOutcome.errEvent == nil {
		t.Fatalf("SEE outcome = %+v, want error event", seeOutcome)
	}
	if seeOutcome.errEvent.Detail != "see failed" {
		t.Fatalf("SEE error detail = %q", seeOutcome.errEvent.Detail)
	}
	if len(seeOutcome.valuePayloads) != 0 {
		t.Fatalf("SEE value payloads = %d, want 0", len(seeOutcome.valuePayloads))
	}

	session.nextSeq = 3
}

func TestLocalRuntimeSessionDirectBuiltinRequests(t *testing.T) {
	runtime, session := openLocalRuntimeSession(t)
	defer closeLocalRuntimeSession(t, runtime, session)

	value, err := session.Eval("control.demo = 42", controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("seed control.demo: %v", err)
	}
	if value != "nil" {
		t.Fatalf("seed value = %q", value)
	}

	value, err = session.Save(controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("Save: %v", err)
	}
	if value != "nil" {
		t.Fatalf("Save value = %q", value)
	}

	if _, err := session.Eval("control.demo = 99", controlCommandTimeout, nil); err != nil {
		t.Fatalf("mutate control.demo: %v", err)
	}

	value, err = session.Restore(controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("Restore: %v", err)
	}
	if value != "nil" {
		t.Fatalf("Restore value = %q", value)
	}

	value, err = session.Eval("control.demo", controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("read restored control.demo: %v", err)
	}
	if value != "42" {
		t.Fatalf("restored control.demo = %q", value)
	}

	var coreOutput bytes.Buffer
	value, err = session.Core("save", controlCommandTimeout, func(data []byte) {
		coreOutput.Write(data)
	})
	if err != nil {
		t.Fatalf("Core: %v", err)
	}
	if value != "nil" {
		t.Fatalf("Core value = %q", value)
	}
	if !strings.Contains(coreOutput.String(), "<native save/0>") {
		t.Fatalf("Core output = %q", coreOutput.String())
	}

	var slotInfoOutput bytes.Buffer
	value, err = session.SlotInfo("save", controlCommandTimeout, func(data []byte) {
		slotInfoOutput.Write(data)
	})
	if err != nil {
		t.Fatalf("SlotInfo: %v", err)
	}
	if value != "nil" {
		t.Fatalf("SlotInfo value = %q", value)
	}
	if !strings.Contains(slotInfoOutput.String(),
		"  owner: runtime builtin") {
		t.Fatalf("SlotInfo output = %q", slotInfoOutput.String())
	}

	value, err = session.Wipe(controlCommandTimeout, nil)
	if err != nil {
		t.Fatalf("Wipe: %v", err)
	}
	if value != "nil" {
		t.Fatalf("Wipe value = %q", value)
	}

	_, err = session.See("control.demo", controlCommandTimeout)
	var controlErr *ControlError
	if !errors.As(err, &controlErr) {
		t.Fatalf("See after wipe error = %v, want ControlError", err)
	}
	if controlErr.Phase != phaseInspect || controlErr.Detail != "see failed" {
		t.Fatalf("See after wipe control error = %+v", controlErr)
	}
}

func openLocalRuntimeRaw(t *testing.T) (*LocalRuntime, *Session) {
	t.Helper()

	runtimePath := findLocalRuntimeBinary(t)
	if runtimePath == "" {
		t.Skip("local Frothy runtime not built")
	}

	runtime, err := StartLocalRuntime(runtimePath)
	if err != nil {
		t.Fatalf("StartLocalRuntime: %v", err)
	}
	session := NewSession(runtime.Transport())
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		runtime.Close()
		t.Fatalf("AcquirePrompt: %v", err)
	}
	return runtime, session
}

func rawSeedSourceFits(source string) bool {
	return len(source)+1 <= rawReplLineLimit
}

func seedRawDefinition(t *testing.T, session *Session, source string) {
	t.Helper()

	if err := session.writeBytes([]byte(source + "\n")); err != nil {
		t.Fatalf("seed raw definition: %v", err)
	}
	if _, err := session.waitForRaw([]byte("frothy> "), rawPromptTimeout); err != nil {
		t.Fatalf("wait for prompt after raw seed: %v", err)
	}
}

func enterLocalRuntimeControl(t *testing.T, runtime *LocalRuntime, session *Session) {
	t.Helper()

	if err := session.EnterControl(rawPromptTimeout); err != nil {
		runtime.Close()
		t.Fatalf("EnterControl: %v", err)
	}
	if _, err := session.Hello(controlCommandTimeout); err != nil {
		runtime.Close()
		t.Fatalf("Hello: %v", err)
	}
}

func openLocalRuntimeSession(t *testing.T) (*LocalRuntime, *Session) {
	runtime, session := openLocalRuntimeRaw(t)
	enterLocalRuntimeControl(t, runtime, session)
	return runtime, session
}

func closeLocalRuntimeSession(t *testing.T, runtime *LocalRuntime, session *Session) {
	t.Helper()

	var detachErr error
	var closeErr error

	if session != nil && session.sessionID != 0 {
		detachErr = session.Detach(controlCommandTimeout)
	}
	if runtime != nil {
		closeErr = runtime.Close()
	}
	if detachErr != nil || closeErr != nil {
		t.Fatalf("Detach: %v; Close local runtime: %v", detachErr, closeErr)
	}
}
