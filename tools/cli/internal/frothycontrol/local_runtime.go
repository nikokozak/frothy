package frothycontrol

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/nikokozak/frothy/tools/cli/internal/serial"
	"golang.org/x/sys/unix"
)

type localTransport struct {
	file        *os.File
	writeTo     *os.File
	readTimeout time.Duration
}

func (t *localTransport) Read(buf []byte) (int, error) {
	timeoutMs := -1
	if t.readTimeout > 0 {
		timeoutMs = int(t.readTimeout / time.Millisecond)
		if timeoutMs == 0 {
			timeoutMs = 1
		}
	}

	fds := []unix.PollFd{{
		Fd:     int32(t.file.Fd()),
		Events: unix.POLLIN,
	}}
	for {
		n, err := unix.Poll(fds, timeoutMs)
		if err == unix.EINTR {
			continue
		}
		if err != nil {
			return 0, err
		}
		if n == 0 {
			return 0, nil
		}
		return t.file.Read(buf)
	}
}

func (t *localTransport) Write(data []byte) error {
	for len(data) > 0 {
		n, err := t.writeTo.Write(data)
		if err != nil {
			return err
		}
		data = data[n:]
	}
	return nil
}

func (t *localTransport) Close() error {
	if err := t.writeTo.Close(); err != nil {
		_ = t.file.Close()
		if isClosedPipeError(err) {
			return nil
		}
		return err
	}
	if err := t.file.Close(); err != nil && !isClosedPipeError(err) {
		return err
	}
	return nil
}

func (t *localTransport) Path() string {
	return t.file.Name()
}

func (t *localTransport) SetReadTimeout(d time.Duration) error {
	t.readTimeout = d
	return nil
}

func (t *localTransport) ResetInputBuffer() {
	buf := make([]byte, 256)
	_ = t.SetReadTimeout(5 * time.Millisecond)
	for {
		n, err := t.Read(buf)
		if err != nil || n == 0 {
			return
		}
	}
}

func (t *localTransport) Drain(duration time.Duration) {
	deadline := time.Now().Add(duration)
	buf := make([]byte, 256)
	for time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		if remaining > 25*time.Millisecond {
			remaining = 25 * time.Millisecond
		}
		_ = t.SetReadTimeout(remaining)
		_, _ = t.Read(buf)
	}
}

type LocalRuntime struct {
	transport *localTransport
	cmd       *exec.Cmd
	tempDir   string
}

func StartLocalRuntime(binary string) (*LocalRuntime, error) {
	tempDir, err := os.MkdirTemp("", "frothy-control-smoke-")
	if err != nil {
		return nil, fmt.Errorf("mktemp: %w", err)
	}
	return StartLocalRuntimeInDir(binary, tempDir, true)
}

func StartLocalRuntimeInDir(binary string, dir string, cleanupDir bool) (*LocalRuntime, error) {
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, fmt.Errorf("mkdir %s: %w", dir, err)
	}

	tempDir := ""
	if cleanupDir {
		tempDir = dir
	}

	cmd := exec.Command(binary)
	cmd.Dir = dir
	stdin, err := cmd.StdinPipe()
	if err != nil {
		if tempDir != "" {
			_ = os.RemoveAll(tempDir)
		}
		return nil, fmt.Errorf("local runtime stdin: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		if tempDir != "" {
			_ = os.RemoveAll(tempDir)
		}
		return nil, fmt.Errorf("local runtime stdout: %w", err)
	}
	if err := cmd.Start(); err != nil {
		if tempDir != "" {
			_ = os.RemoveAll(tempDir)
		}
		return nil, fmt.Errorf("start local runtime: %w", err)
	}

	return &LocalRuntime{
		transport: &localTransport{
			file:    stdout.(*os.File),
			writeTo: stdin.(*os.File),
		},
		cmd:     cmd,
		tempDir: tempDir,
	}, nil
}

func (r *LocalRuntime) Transport() serial.Transport {
	return r.transport
}

func (r *LocalRuntime) Interrupt() error {
	return r.cmd.Process.Signal(os.Interrupt)
}

func (r *LocalRuntime) Close() error {
	var closeErr error

	if r.transport != nil {
		_ = r.transport.Write([]byte("quit\n"))
		done := make(chan error, 1)
		go func() {
			done <- r.cmd.Wait()
		}()
		select {
		case err := <-done:
			if err != nil {
				closeErr = err
			}
		case <-time.After(1 * time.Second):
			_ = r.cmd.Process.Kill()
			<-done
		}
		if err := r.transport.Close(); err != nil &&
			!isClosedPipeError(err) && closeErr == nil {
			closeErr = err
		}
		r.transport = nil
	}

	if r.tempDir != "" {
		_ = os.RemoveAll(r.tempDir)
		r.tempDir = ""
	}
	return closeErr
}

func isClosedPipeError(err error) bool {
	return errors.Is(err, os.ErrClosed) ||
		strings.Contains(err.Error(), "file already closed")
}
