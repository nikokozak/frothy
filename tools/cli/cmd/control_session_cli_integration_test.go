//go:build integration

package cmd

import (
	"bufio"
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"io"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

func TestIntegrationToolingControlSessionLocalRuntime(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	if out, err := runIntegrationCLI(t, workspace, env, 2*time.Minute, "new", "control-session"); err != nil {
		t.Fatalf("froth new failed: %v\n%s", err, out)
	}

	projectDir := filepath.Join(workspace, "control-session")
	if out, err := runIntegrationCLI(t, projectDir, env, 3*time.Minute, "build"); err != nil {
		t.Fatalf("froth build failed: %v\n%s", err, out)
	}

	runtimePath := filepath.Join(projectDir, ".froth-build", "firmware", "Frothy")
	session := startControlSessionCLI(t, projectDir, env, runtimePath)
	defer session.close(t)

	session.send(t, map[string]any{"id": 1, "command": "connect"})
	if event := session.next(t); event["event"] != "connected" {
		t.Fatalf("connect event = %v", event)
	}
	if response := session.next(t); response["ok"] != true {
		t.Fatalf("connect response = %v", response)
	}

	session.send(t, map[string]any{
		"id":      2,
		"command": "eval",
		"source":  "control.demo = 42",
	})
	evalResponse := session.expectValueCycle(t, 2)
	if got := textResult(t, evalResponse); got != "nil" {
		t.Fatalf("eval result = %q, want nil", got)
	}

	session.send(t, map[string]any{"id": 3, "command": "words"})
	wordsResponse := session.expectValueCycle(t, 3)
	if !contains(wordsResult(t, wordsResponse), "control.demo") {
		t.Fatalf("words result = %v, want control.demo", wordsResponse["result"])
	}

	session.send(t, map[string]any{
		"id":      4,
		"command": "see",
		"name":    "control.demo",
	})
	seeResponse := session.expectValueCycle(t, 4)
	seeResult, _ := seeResponse["result"].(map[string]any)
	if seeResult["rendered"] != "42" {
		t.Fatalf("see result = %v", seeResult)
	}

	session.send(t, map[string]any{
		"id":      5,
		"command": "core",
		"name":    "save",
	})
	coreResponse, coreOutput := session.expectValueCycleWithOutput(t, 5)
	if !bytes.Contains(coreOutput, []byte("<native save/0>")) {
		t.Fatalf("core output = %q, want native save rendering", coreOutput)
	}
	if got := textResult(t, coreResponse); got != "nil" {
		t.Fatalf("core result = %q, want nil", got)
	}

	session.send(t, map[string]any{"id": 6, "command": "disconnect"})
	session.expectDisconnectCycle(t, 6)
}

type controlSessionCLI struct {
	stdin  io.WriteCloser
	cmd    *exec.Cmd
	cancel context.CancelFunc
	done   chan error
	lines  chan map[string]any
	stderr bytes.Buffer
}

func startControlSessionCLI(t *testing.T, dir string, env []string, runtimePath string) *controlSessionCLI {
	t.Helper()

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	cmd := exec.CommandContext(ctx, integrationCLIPath,
		"tooling", "control-session", "--local-runtime", runtimePath)
	cmd.Dir = dir
	cmd.Env = env

	stdin, err := cmd.StdinPipe()
	if err != nil {
		cancel()
		t.Fatalf("stdin pipe: %v", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		cancel()
		t.Fatalf("stdout pipe: %v", err)
	}
	session := &controlSessionCLI{
		stdin:  stdin,
		cmd:    cmd,
		cancel: cancel,
		done:   make(chan error, 1),
		lines:  make(chan map[string]any, 32),
	}
	cmd.Stderr = &session.stderr

	if err := cmd.Start(); err != nil {
		cancel()
		t.Fatalf("start control-session helper: %v", err)
	}

	go func() {
		scanner := bufio.NewScanner(stdout)
		scanner.Buffer(make([]byte, 0, 1024), 1024*1024)
		for scanner.Scan() {
			var line map[string]any
			if err := json.Unmarshal(scanner.Bytes(), &line); err != nil {
				line = map[string]any{
					"type":  "decode_error",
					"error": err.Error(),
					"raw":   scanner.Text(),
				}
			}
			session.lines <- line
		}
		close(session.lines)
	}()

	go func() {
		session.done <- cmd.Wait()
	}()

	return session
}

func (s *controlSessionCLI) send(t *testing.T, request map[string]any) {
	t.Helper()

	line, err := json.Marshal(request)
	if err != nil {
		t.Fatalf("marshal request: %v", err)
	}
	if _, err := s.stdin.Write(append(line, '\n')); err != nil {
		t.Fatalf("write request: %v\nstderr:\n%s", err, s.stderr.String())
	}
}

func (s *controlSessionCLI) next(t *testing.T) map[string]any {
	t.Helper()

	select {
	case line, ok := <-s.lines:
		if !ok {
			t.Fatalf("helper output closed\nstderr:\n%s", s.stderr.String())
		}
		return line
	case <-time.After(10 * time.Second):
		t.Fatalf("timed out waiting for helper output\nstderr:\n%s", s.stderr.String())
		return nil
	}
}

func (s *controlSessionCLI) expectValueCycle(t *testing.T, requestID float64) map[string]any {
	t.Helper()

	response, _ := s.expectValueCycleWithOutput(t, requestID)
	return response
}

func (s *controlSessionCLI) expectValueCycleWithOutput(t *testing.T, requestID float64) (map[string]any, []byte) {
	t.Helper()

	var output bytes.Buffer

	for {
		event := s.next(t)
		if event["event"] == "output" {
			output.Write(decodeOutput(t, event))
			continue
		}
		if event["event"] != "value" {
			t.Fatalf("value event = %v", event)
		}
		break
	}
	if event := s.next(t); event["event"] != "idle" {
		t.Fatalf("idle event = %v", event)
	}
	response := s.next(t)
	if response["ok"] != true {
		t.Fatalf("response = %v", response)
	}
	if response["id"] != requestID {
		t.Fatalf("response id = %v, want %v", response["id"], requestID)
	}
	return response, output.Bytes()
}

func (s *controlSessionCLI) expectValueCycleNoOutput(t *testing.T, requestID float64) map[string]any {
	t.Helper()

	if event := s.next(t); event["event"] != "value" {
		t.Fatalf("value event = %v", event)
	}
	if event := s.next(t); event["event"] != "idle" {
		t.Fatalf("idle event = %v", event)
	}
	response := s.next(t)
	if response["ok"] != true {
		t.Fatalf("response = %v", response)
	}
	if response["id"] != requestID {
		t.Fatalf("response id = %v, want %v", response["id"], requestID)
	}
	return response
}

func (s *controlSessionCLI) expectDisconnectCycle(t *testing.T,
	requestID float64) map[string]any {
	t.Helper()

	sawCurrentDisconnect := false
	for {
		message := s.next(t)
		if message["event"] == "disconnected" {
			if message["request_id"] == requestID {
				sawCurrentDisconnect = true
			}
			continue
		}
		if message["ok"] != true {
			t.Fatalf("disconnect response = %v", message)
		}
		if message["id"] != requestID {
			t.Fatalf("disconnect response id = %v, want %v", message["id"], requestID)
		}
		if !sawCurrentDisconnect {
			t.Fatalf("disconnect response arrived before current disconnect event: %v", message)
		}
		return message
	}
}

func (s *controlSessionCLI) close(t *testing.T) {
	t.Helper()

	_ = s.stdin.Close()
	select {
	case err := <-s.done:
		s.cancel()
		if err != nil {
			t.Fatalf("helper exit: %v\nstderr:\n%s", err, s.stderr.String())
		}
	case <-time.After(10 * time.Second):
		s.cancel()
		t.Fatalf("timed out waiting for helper exit\nstderr:\n%s", s.stderr.String())
	}
}

func textResult(t *testing.T, response map[string]any) string {
	t.Helper()

	result, _ := response["result"].(map[string]any)
	text, _ := result["text"].(string)
	return text
}

func wordsResult(t *testing.T, response map[string]any) []string {
	t.Helper()

	result, _ := response["result"].(map[string]any)
	rawWords, _ := result["words"].([]any)
	words := make([]string, 0, len(rawWords))
	for _, rawWord := range rawWords {
		if word, ok := rawWord.(string); ok {
			words = append(words, word)
		}
	}
	return words
}

func decodeOutput(t *testing.T, event map[string]any) []byte {
	t.Helper()

	data, _ := event["data"].(string)
	decoded, err := base64.StdEncoding.DecodeString(data)
	if err != nil {
		t.Fatalf("decode output payload: %v", err)
	}
	return decoded
}
