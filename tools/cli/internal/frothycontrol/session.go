package frothycontrol

import (
	"bytes"
	"errors"
	"fmt"
	"sync"
	"time"

	baseproto "github.com/nikokozak/frothy/tools/cli/internal/protocol"
	"github.com/nikokozak/frothy/tools/cli/internal/serial"
)

var ErrInterrupted = errors.New("control request interrupted")

type rawWaitTimeoutError struct {
	needle string
}

func (e rawWaitTimeoutError) Error() string {
	return fmt.Sprintf("timed out waiting for %q", e.needle)
}

func isRawWaitTimeout(err error) bool {
	var timeoutErr rawWaitTimeoutError
	return errors.As(err, &timeoutErr)
}

type Session struct {
	transport serial.Transport
	sessionID uint64
	nextSeq   uint16
	writeMu   sync.Mutex
}

type requestOutcome struct {
	hello         *baseproto.HelloResponse
	valuePayloads [][]byte
	errEvent      *ControlError
	interrupted   bool
}

func NewSession(transport serial.Transport) *Session {
	return &Session{transport: transport}
}

func OpenSerial(portPath string) (serial.Transport, error) {
	if portPath == "" {
		path, err := serial.DiscoverPath()
		if err != nil {
			return nil, err
		}
		return serial.Open(path)
	}
	return serial.Open(portPath)
}

func (s *Session) waitForRaw(needle []byte, timeout time.Duration) ([]byte, error) {
	deadline := time.Now().Add(timeout)
	buf := make([]byte, 256)
	var transcript []byte

	for {
		if bytes.Contains(transcript, needle) {
			return transcript, nil
		}
		remaining := time.Until(deadline)
		if remaining <= 0 {
			return transcript, rawWaitTimeoutError{needle: string(needle)}
		}
		if remaining > 250*time.Millisecond {
			remaining = 250 * time.Millisecond
		}
		if err := s.transport.SetReadTimeout(remaining); err != nil {
			return transcript, err
		}
		n, err := s.transport.Read(buf)
		if err != nil {
			return transcript, err
		}
		if n == 0 {
			continue
		}
		transcript = append(transcript, buf[:n]...)
	}
}

func (s *Session) AcquirePrompt(timeout time.Duration) error {
	if _, err := s.waitForRaw([]byte("frothy> "), timeout); err == nil {
		return nil
	} else if !isRawWaitTimeout(err) {
		return err
	}
	if err := s.writeBytes([]byte{0x03}); err != nil {
		return fmt.Errorf("send ctrl-c: %w", err)
	}
	if _, err := s.waitForRaw([]byte("frothy> "), timeout/2); err == nil {
		return nil
	} else if !isRawWaitTimeout(err) {
		return err
	}
	if err := s.writeBytes([]byte{'\n'}); err != nil {
		return fmt.Errorf("send newline: %w", err)
	}
	_, err := s.waitForRaw([]byte("frothy> "), timeout)
	return err
}

func (s *Session) EnterControl(timeout time.Duration) error {
	if err := s.writeBytes([]byte(".control\n")); err != nil {
		return fmt.Errorf("enter control: %w", err)
	}
	if _, err := s.waitForRaw([]byte("control: ready"), timeout); err != nil {
		return err
	}

	// The shell prints "control: ready" immediately before it hands control to
	// the framed session loop. Give the device a beat to finish that transition
	// so the first HELLO frame is not sent into the handoff gap.
	time.Sleep(150 * time.Millisecond)
	return nil
}

func (s *Session) Interrupt() error {
	return s.writeBytes([]byte{0x03})
}

func (s *Session) Close() error {
	return s.transport.Close()
}

func (s *Session) runRequest(msgType byte, seq uint16, payload []byte,
	timeout time.Duration, onOutput func([]byte)) (*requestOutcome, error) {
	wire, err := encodeWireFrame(s.sessionID, msgType, seq, payload)
	if err != nil {
		return nil, err
	}
	if err := s.writeBytes(wire); err != nil {
		return nil, err
	}

	noTimeout := timeout == 0
	deadline := time.Now()
	if !noTimeout {
		deadline = time.Now().Add(timeout)
	}
	outcome := &requestOutcome{}
	for {
		remaining := time.Duration(0)
		if !noTimeout {
			remaining = time.Until(deadline)
			if remaining <= 0 {
				return nil, fmt.Errorf("device response timeout")
			}
		}
		encoded, err := serial.ReadFrameTransport(s.transport, remaining, nil)
		if err != nil {
			return nil, err
		}
		header, payload, err := decodeFrame(encoded)
		if err != nil {
			return nil, fmt.Errorf("decode frame: %w", err)
		}
		if header.SessionID != s.sessionID || header.Seq != seq {
			continue
		}
		switch header.MessageType {
		case helloEvt:
			hello, err := baseproto.ParseHelloResponse(payload)
			if err != nil {
				return nil, err
			}
			outcome.hello = hello
		case outputEvt:
			data, err := baseproto.ParseOutputData(payload)
			if err != nil {
				return nil, err
			}
			if onOutput != nil {
				onOutput(data)
			}
		case valueEvt:
			outcome.valuePayloads = append(outcome.valuePayloads,
				append([]byte(nil), payload...))
		case errorEvt:
			errEvt, err := parseControlError(payload)
			if err != nil {
				return nil, err
			}
			outcome.errEvent = errEvt
		case interruptEvt:
			outcome.interrupted = true
		case idleEvt:
			return outcome, nil
		}
	}
}

func singleValuePayload(payloads [][]byte, label string) ([]byte, error) {
	if len(payloads) == 0 {
		return nil, fmt.Errorf("missing %s value", label)
	}
	if len(payloads) != 1 {
		return nil, fmt.Errorf("expected one %s value, got %d", label, len(payloads))
	}
	return payloads[0], nil
}

func (s *Session) Hello(timeout time.Duration) (*baseproto.HelloResponse, error) {
	sessionID, err := baseproto.GenerateSessionID()
	if err != nil {
		return nil, err
	}

	s.sessionID = sessionID
	outcome, err := s.runRequest(helloReq, 0, nil, timeout, nil)
	if err != nil {
		s.sessionID = 0
		return nil, err
	}
	if outcome.errEvent != nil {
		s.sessionID = 0
		return nil, outcome.errEvent
	}
	if outcome.hello == nil {
		s.sessionID = 0
		return nil, fmt.Errorf("missing HELLO event")
	}
	s.nextSeq = 1
	return outcome.hello, nil
}

func (s *Session) Eval(source string, timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(evalReq, buildStringPayload(source), timeout, onOutput,
		"EVAL")
}

func (s *Session) Save(timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(saveReq, nil, timeout, onOutput, "SAVE")
}

func (s *Session) Restore(timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(restoreReq, nil, timeout, onOutput, "RESTORE")
}

func (s *Session) Wipe(timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(wipeReq, nil, timeout, onOutput, "WIPE")
}

func (s *Session) Core(name string, timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(coreReq, buildStringPayload(name), timeout, onOutput,
		"CORE")
}

func (s *Session) SlotInfo(name string, timeout time.Duration,
	onOutput func([]byte)) (string, error) {
	return s.runTextRequest(slotInfoReq, buildStringPayload(name), timeout,
		onOutput, "SLOT_INFO")
}

func (s *Session) runTextRequest(msgType byte, payload []byte,
	timeout time.Duration, onOutput func([]byte), label string) (string, error) {
	seq := s.nextSeq
	outcome, err := s.runRequest(msgType, seq, payload, timeout, onOutput)
	if err != nil {
		return "", err
	}
	s.nextSeq++
	if s.nextSeq == 0 {
		s.nextSeq = 1
	}
	if outcome.errEvent != nil {
		return "", outcome.errEvent
	}
	if outcome.interrupted {
		return "", ErrInterrupted
	}
	valuePayload, err := singleValuePayload(outcome.valuePayloads, label)
	if err != nil {
		return "", err
	}
	return parseStringPayload(valuePayload)
}

func (s *Session) Words(timeout time.Duration) ([]string, error) {
	seq := s.nextSeq
	outcome, err := s.runRequest(wordsReq, seq, nil, timeout, nil)
	if err != nil {
		return nil, err
	}
	s.nextSeq++
	if s.nextSeq == 0 {
		s.nextSeq = 1
	}
	if outcome.errEvent != nil {
		return nil, outcome.errEvent
	}
	return parseWordsPayloads(outcome.valuePayloads)
}

func (s *Session) See(name string, timeout time.Duration) (*SeeResult, error) {
	seq := s.nextSeq
	outcome, err := s.runRequest(seeReq, seq, buildStringPayload(name), timeout, nil)
	if err != nil {
		return nil, err
	}
	s.nextSeq++
	if s.nextSeq == 0 {
		s.nextSeq = 1
	}
	if outcome.errEvent != nil {
		return nil, outcome.errEvent
	}
	return parseSeePayloads(outcome.valuePayloads)
}

func (s *Session) Reset(timeout time.Duration) (*baseproto.ResetResponse, error) {
	seq := s.nextSeq
	outcome, err := s.runRequest(resetReq, seq, nil, timeout, nil)
	if err != nil {
		return nil, err
	}
	s.nextSeq++
	if s.nextSeq == 0 {
		s.nextSeq = 1
	}
	if outcome.errEvent != nil {
		return nil, outcome.errEvent
	}
	payload, err := singleValuePayload(outcome.valuePayloads, "RESET")
	if err != nil {
		return nil, err
	}
	return baseproto.ParseResetResponse(payload)
}

func (s *Session) Detach(timeout time.Duration) error {
	seq := s.nextSeq
	outcome, err := s.runRequest(detachReq, seq, nil, timeout, nil)
	if err != nil {
		return err
	}
	s.sessionID = 0
	s.nextSeq = 0
	if outcome.errEvent != nil {
		return outcome.errEvent
	}
	return nil
}

func (s *Session) writeBytes(data []byte) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	return s.transport.Write(data)
}
