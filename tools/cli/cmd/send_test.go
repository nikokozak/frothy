package cmd

import (
	"strings"
	"testing"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
)

type stubSendRuntime struct {
	resetCalls int
	resetErr   error
	words      []string
	wordsErr   error
}

func (r *stubSendRuntime) Eval(string, func([]byte)) (string, error) {
	return "", nil
}

func (r *stubSendRuntime) Interrupt() error {
	return nil
}

func (r *stubSendRuntime) Reset() (*baseproto.ResetResponse, error) {
	r.resetCalls++
	return &baseproto.ResetResponse{}, r.resetErr
}

func (r *stubSendRuntime) Words() ([]string, error) {
	return r.words, r.wordsErr
}

func withSendRunners(t *testing.T,
	sourceRunner func(controlEvalManager, string) (string, error),
	evalRunner func(controlEvalManager, string) (string, error)) {
	t.Helper()

	oldSource := sendControlSource
	oldEval := sendControlEval
	sendControlSource = sourceRunner
	sendControlEval = evalRunner
	t.Cleanup(func() {
		sendControlSource = oldSource
		sendControlEval = oldEval
	})
}

func TestSendResolvedPayloadResetsBeforeWholeFileReplay(t *testing.T) {
	runtime := &stubSendRuntime{}
	var replayed []string

	withSendRunners(t,
		func(_ controlEvalManager, source string) (string, error) {
			replayed = append(replayed, source)
			return "nil", nil
		},
		func(controlEvalManager, string) (string, error) {
			t.Fatal("entrypoint eval should not run")
			return "", nil
		},
	)

	err := sendResolvedPayload(runtime, &sendPayload{
		source:          "control.demo = 42",
		resetBeforeEval: true,
	})
	if err != nil {
		t.Fatalf("sendResolvedPayload: %v", err)
	}
	if runtime.resetCalls != 1 {
		t.Fatalf("resetCalls = %d, want 1", runtime.resetCalls)
	}
	if len(replayed) != 1 || replayed[0] != "control.demo = 42" {
		t.Fatalf("replayed = %v", replayed)
	}
}

func TestSendResolvedPayloadLeavesRawSourceAdditive(t *testing.T) {
	runtime := &stubSendRuntime{}
	var replayed []string

	withSendRunners(t,
		func(_ controlEvalManager, source string) (string, error) {
			replayed = append(replayed, source)
			return "nil", nil
		},
		func(controlEvalManager, string) (string, error) {
			t.Fatal("entrypoint eval should not run")
			return "", nil
		},
	)

	err := sendResolvedPayload(runtime, &sendPayload{
		source: "1 + 1",
	})
	if err != nil {
		t.Fatalf("sendResolvedPayload: %v", err)
	}
	if runtime.resetCalls != 0 {
		t.Fatalf("resetCalls = %d, want 0", runtime.resetCalls)
	}
	if len(replayed) != 1 || replayed[0] != "1 + 1" {
		t.Fatalf("replayed = %v", replayed)
	}
}

func TestSendResolvedPayloadRunsBootAfterResetReplay(t *testing.T) {
	runtime := &stubSendRuntime{words: []string{"boot"}}
	var replayed []string
	var entries []string

	withSendRunners(t,
		func(_ controlEvalManager, source string) (string, error) {
			replayed = append(replayed, source)
			return "nil", nil
		},
		func(_ controlEvalManager, source string) (string, error) {
			entries = append(entries, source)
			return "nil", nil
		},
	)

	err := sendResolvedPayload(runtime, &sendPayload{
		source:          "control.demo = 42",
		resetBeforeEval: true,
	})
	if err != nil {
		t.Fatalf("sendResolvedPayload: %v", err)
	}
	if len(replayed) != 1 {
		t.Fatalf("replayed = %v, want one replay", replayed)
	}
	if len(entries) != 1 || entries[0] != "boot()" {
		t.Fatalf("entries = %v, want boot()", entries)
	}
}

func TestSendResolvedPayloadFailsClosedOnResetUnavailable(t *testing.T) {
	runtime := &stubSendRuntime{resetErr: frothycontrol.ErrResetUnavailable}
	replayed := false

	withSendRunners(t,
		func(controlEvalManager, string) (string, error) {
			replayed = true
			return "nil", nil
		},
		func(controlEvalManager, string) (string, error) {
			t.Fatal("entrypoint eval should not run")
			return "", nil
		},
	)

	err := sendResolvedPayload(runtime, &sendPayload{
		source:          "control.demo = 42",
		resetBeforeEval: true,
	})
	if err == nil {
		t.Fatal("sendResolvedPayload succeeded, want error")
	}
	if replayed {
		t.Fatal("replay ran after reset_unavailable")
	}
	if !strings.Contains(err.Error(), "reset + eval") ||
		!strings.Contains(err.Error(), "too old for safe whole-file send") {
		t.Fatalf("error = %v", err)
	}
}
