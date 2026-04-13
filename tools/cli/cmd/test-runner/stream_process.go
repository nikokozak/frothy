package main

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"sync"
	"time"
)

func startStreamProcess(binaryPath string, cwd string) (*streamProcess, error) {
	reader, writer, err := os.Pipe()
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(binaryPath)
	cmd.Dir = cwd
	stdin, err := cmd.StdinPipe()
	if err != nil {
		reader.Close()
		writer.Close()
		return nil, err
	}
	cmd.Stdout = writer
	cmd.Stderr = writer
	if err := cmd.Start(); err != nil {
		reader.Close()
		writer.Close()
		return nil, err
	}
	_ = writer.Close()

	process := &streamProcess{
		cmd:      cmd,
		stdin:    stdin,
		waitDone: make(chan struct{}),
	}
	process.outputCond = sync.NewCond(&process.outputMu)

	go func() {
		buf := make([]byte, 4096)
		for {
			n, readErr := reader.Read(buf)
			if n > 0 {
				chunk := append([]byte(nil), buf[:n]...)
				process.outputMu.Lock()
				process.output = append(process.output, chunk...)
				process.outputCond.Broadcast()
				process.outputMu.Unlock()
			}
			if readErr != nil {
				_ = reader.Close()
				process.outputMu.Lock()
				process.closed = true
				process.outputCond.Broadcast()
				process.outputMu.Unlock()
				return
			}
		}
	}()

	go func() {
		process.waitErr = cmd.Wait()
		close(process.waitDone)
	}()

	return process, nil
}

func (p *streamProcess) waitFor(match func([]byte) bool, timeout time.Duration) ([]byte, error) {
	deadline := time.Now().Add(timeout)
	p.outputMu.Lock()
	defer p.outputMu.Unlock()
	if len(p.pending) > 0 {
		p.output = append(p.pending, p.output...)
		p.pending = nil
	}
	for {
		data := append([]byte(nil), p.output...)
		if match(data) {
			p.output = p.output[:0]
			return data, nil
		}
		if p.closed {
			<-p.waitDone
			waitErr := p.waitErr
			if waitErr == nil {
				waitErr = io.EOF
			}
			return data, fmt.Errorf("process exited before expected output: %w\n%s", waitErr, normalizeNewlines(data))
		}
		remaining := time.Until(deadline)
		if remaining <= 0 {
			return data, fmt.Errorf("timed out waiting for process output\n%s", normalizeNewlines(data))
		}
		timer := time.AfterFunc(remaining, func() {
			p.outputCond.Broadcast()
		})
		p.outputCond.Wait()
		timer.Stop()
	}
}

func (p *streamProcess) send(text string) error {
	if p.stdin == nil {
		return fmt.Errorf("stdin is closed")
	}
	_, err := io.WriteString(p.stdin, text)
	return err
}

func (p *streamProcess) interrupt() error {
	if p.cmd == nil || p.cmd.Process == nil {
		return fmt.Errorf("process is not running")
	}
	return p.cmd.Process.Signal(os.Interrupt)
}

func (p *streamProcess) closeWith(text string) {
	if p == nil || p.cmd == nil {
		return
	}
	if text != "" && p.stdin != nil {
		_, _ = io.WriteString(p.stdin, text)
	}
	if p.stdin != nil {
		_ = p.stdin.Close()
		p.stdin = nil
	}
	done := make(chan struct{})
	go func() {
		<-p.waitDone
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		_ = p.cmd.Process.Kill()
		<-done
	}
}
