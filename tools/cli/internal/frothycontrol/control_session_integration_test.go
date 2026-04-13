package frothycontrol

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func TestControlSessionServerLocalRuntime(t *testing.T) {
	runtimePath := findLocalRuntimeBinary(t)
	if runtimePath == "" {
		t.Skip("local Frothy runtime not built")
	}

	manager := NewManager(ManagerConfig{LocalRuntimePath: runtimePath})
	harness := startHarness(t, manager)
	defer harness.close(t)

	harness.send(t, controlSessionRequest{ID: 1, Command: "connect"})
	if event := harness.next(t); event["event"] != "connected" {
		t.Fatalf("connect event = %v", event)
	}
	if response := harness.next(t); response["ok"] != true {
		t.Fatalf("connect response = %v", response)
	}

	harness.send(t, controlSessionRequest{ID: 2, Command: "eval", Source: "control.demo = 42"})
	consumeValueCycle(t, harness, 2)

	harness.send(t, controlSessionRequest{ID: 3, Command: "see", Name: "control.demo"})
	seeResponse := consumeValueCycle(t, harness, 3)
	seeResult, _ := seeResponse["result"].(map[string]any)
	if seeResult["rendered"] != "42" {
		t.Fatalf("see result = %v", seeResult)
	}

	harness.send(t, controlSessionRequest{ID: 4, Command: "reset"})
	resetResponse := consumeValueCycle(t, harness, 4)
	resetResult, _ := resetResponse["result"].(map[string]any)
	if resetResult["heap_size"] == nil || resetResult["version"] == nil {
		t.Fatalf("reset result = %v", resetResult)
	}

	harness.send(t, controlSessionRequest{ID: 5, Command: "see", Name: "control.demo"})
	seeMissing := consumeErrorCycle(t, harness, 5)
	seeMissingError, _ := seeMissing["error"].(map[string]any)
	if seeMissingError["code"] != "control_error" {
		t.Fatalf("see after reset error = %v", seeMissingError)
	}

	harness.send(t, controlSessionRequest{ID: 6, Command: "save"})
	consumeValueCycle(t, harness, 6)

	harness.send(t, controlSessionRequest{ID: 7, Command: "disconnect"})
	if event := harness.next(t); event["event"] != "disconnected" {
		t.Fatalf("disconnect event = %v", event)
	}
	if response := harness.next(t); response["ok"] != true {
		t.Fatalf("disconnect response = %v", response)
	}
}

func consumeValueCycle(t *testing.T, harness *sessionHarness, requestID float64) map[string]any {
	t.Helper()

	if event := harness.next(t); event["event"] != "value" {
		t.Fatalf("value event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("idle event = %v", event)
	}
	response := harness.next(t)
	if response["ok"] != true {
		t.Fatalf("response = %v", response)
	}
	if response["id"] != requestID {
		t.Fatalf("response id = %v, want %v", response["id"], requestID)
	}
	return response
}

func consumeErrorCycle(t *testing.T, harness *sessionHarness, requestID float64) map[string]any {
	t.Helper()

	if event := harness.next(t); event["event"] != "error" {
		t.Fatalf("error event = %v", event)
	}
	if event := harness.next(t); event["event"] != "idle" {
		t.Fatalf("idle event = %v", event)
	}
	response := harness.next(t)
	if response["ok"] != false {
		t.Fatalf("response = %v", response)
	}
	if response["id"] != requestID {
		t.Fatalf("response id = %v, want %v", response["id"], requestID)
	}
	return response
}

func findLocalRuntimeBinary(t *testing.T) string {
	t.Helper()

	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return ""
	}
	root := filepath.Clean(filepath.Join(filepath.Dir(file), "..", "..", "..", ".."))
	candidates := []string{
		filepath.Join(root, "build", "Frothy"),
		filepath.Join(root, "build", "Frothy.exe"),
	}

	for _, candidate := range candidates {
		info, err := os.Stat(candidate)
		if err == nil && !info.IsDir() {
			return candidate
		}
	}
	return ""
}
