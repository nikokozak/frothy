package frothycontrol

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"

	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
)

type HelperBackend interface {
	IsConnected() bool
	Connect(port string) (*DeviceInfo, error)
	Disconnect() error
	Eval(source string, onOutput func([]byte)) (string, error)
	Reset() (*baseproto.ResetResponse, error)
	Interrupt() error
	Words() ([]string, error)
	See(name string) (*SeeResult, error)
	Save(onOutput func([]byte)) (string, error)
	Restore(onOutput func([]byte)) (string, error)
	Wipe(onOutput func([]byte)) (string, error)
	Core(name string, onOutput func([]byte)) (string, error)
	SlotInfo(name string, onOutput func([]byte)) (string, error)
}

type controlSessionServer struct {
	backend HelperBackend
	writer  io.Writer
	writeMu sync.Mutex
}

type controlSessionRequest struct {
	ID      int64  `json:"id"`
	Command string `json:"command"`
	Port    string `json:"port,omitempty"`
	Source  string `json:"source,omitempty"`
	Name    string `json:"name,omitempty"`
}

type controlSessionResponse struct {
	Type   string       `json:"type"`
	ID     int64        `json:"id"`
	OK     bool         `json:"ok"`
	Result any          `json:"result,omitempty"`
	Error  *helperError `json:"error,omitempty"`
}

type controlSessionEvent struct {
	Type      string       `json:"type"`
	Event     string       `json:"event"`
	RequestID int64        `json:"request_id,omitempty"`
	Data      string       `json:"data,omitempty"`
	Value     any          `json:"value,omitempty"`
	Device    *DeviceInfo  `json:"device,omitempty"`
	Error     *helperError `json:"error,omitempty"`
}

type helperError struct {
	Code       string             `json:"code"`
	Message    string             `json:"message"`
	Phase      uint8              `json:"phase,omitempty"`
	DetailCode uint16             `json:"detail_code,omitempty"`
	Candidates []ConnectCandidate `json:"candidates,omitempty"`
}

type textValue struct {
	Text string `json:"text"`
}

type wordsValue struct {
	Words []string `json:"words"`
}

type resetValue struct {
	Status           uint32 `json:"status,omitempty"`
	HeapSize         uint32 `json:"heap_size"`
	HeapUsed         uint32 `json:"heap_used"`
	HeapOverlayUsed  uint32 `json:"heap_overlay_used,omitempty"`
	SlotCount        uint16 `json:"slot_count,omitempty"`
	SlotOverlayCount uint16 `json:"slot_overlay_count,omitempty"`
	Flags            uint8  `json:"flags,omitempty"`
	Version          string `json:"version,omitempty"`
}

func RunControlSessionServer(backend HelperBackend, reader io.Reader,
	writer io.Writer) error {
	server := &controlSessionServer{
		backend: backend,
		writer:  writer,
	}

	buffered := bufio.NewReader(reader)

	for {
		line, err := buffered.ReadBytes('\n')
		if err != nil && len(line) == 0 {
			_ = backend.Disconnect()
			if errors.Is(err, io.EOF) {
				return nil
			}
			return err
		}
		line = bytes.TrimSpace(line)
		if len(line) == 0 {
			if err != nil {
				_ = backend.Disconnect()
				if errors.Is(err, io.EOF) {
					return nil
				}
				return err
			}
			continue
		}

		var request controlSessionRequest
		if err := json.Unmarshal(line, &request); err != nil {
			server.sendResponse(controlSessionResponse{
				Type:  "response",
				ID:    0,
				OK:    false,
				Error: helperErrorFrom(err),
			})
			continue
		}

		go server.handleRequest(request)

		if err != nil {
			_ = backend.Disconnect()
			if errors.Is(err, io.EOF) {
				return nil
			}
			return err
		}
	}
}

func (s *controlSessionServer) handleRequest(request controlSessionRequest) {
	switch request.Command {
	case "connect":
		device, err := s.backend.Connect(request.Port)
		if err != nil {
			s.sendResponse(controlSessionResponse{
				Type:  "response",
				ID:    request.ID,
				OK:    false,
				Error: helperErrorFrom(err),
			})
			return
		}
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "connected",
			RequestID: request.ID,
			Device:    device,
		})
		s.sendResponse(controlSessionResponse{
			Type:   "response",
			ID:     request.ID,
			OK:     true,
			Result: device,
		})
	case "disconnect":
		err := s.backend.Disconnect()
		if err == nil {
			s.sendEvent(controlSessionEvent{
				Type:      "event",
				Event:     "disconnected",
				RequestID: request.ID,
			})
		}
		s.finishSimple(request.ID, nil, err)
	case "interrupt":
		s.finishSimple(request.ID, nil, s.backend.Interrupt())
	case "eval":
		s.runTextRequest(request.ID, func(onOutput func([]byte)) (string, error) {
			return s.backend.Eval(request.Source, onOutput)
		})
	case "save":
		s.runTextRequest(request.ID, s.backend.Save)
	case "restore":
		s.runTextRequest(request.ID, s.backend.Restore)
	case "wipe":
		s.runTextRequest(request.ID, s.backend.Wipe)
	case "core":
		s.runTextRequest(request.ID, func(onOutput func([]byte)) (string, error) {
			return s.backend.Core(request.Name, onOutput)
		})
	case "slot_info":
		s.runTextRequest(request.ID, func(onOutput func([]byte)) (string, error) {
			return s.backend.SlotInfo(request.Name, onOutput)
		})
	case "reset":
		result, err := s.backend.Reset()
		s.finishValueRequest(request.ID, resetValueFrom(result), err)
	case "words":
		names, err := s.backend.Words()
		s.finishValueRequest(request.ID, wordsValue{Words: names}, err)
	case "see":
		view, err := s.backend.See(request.Name)
		s.finishValueRequest(request.ID, view, err)
	default:
		s.sendResponse(controlSessionResponse{
			Type:  "response",
			ID:    request.ID,
			OK:    false,
			Error: &helperError{Code: "unknown_command", Message: fmt.Sprintf("unknown command: %s", request.Command)},
		})
	}
}

func (s *controlSessionServer) runTextRequest(requestID int64,
	run func(onOutput func([]byte)) (string, error)) {
	value, err := run(func(data []byte) {
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "output",
			RequestID: requestID,
			Data:      base64.StdEncoding.EncodeToString(data),
		})
	})
	s.finishValueRequest(requestID, textValue{Text: value}, err)
}

func (s *controlSessionServer) finishSimple(requestID int64, result any, err error) {
	if err == nil {
		s.sendResponse(controlSessionResponse{
			Type:   "response",
			ID:     requestID,
			OK:     true,
			Result: result,
		})
		return
	}
	s.sendResponse(controlSessionResponse{
		Type:  "response",
		ID:    requestID,
		OK:    false,
		Error: helperErrorFrom(err),
	})
	if !s.backend.IsConnected() {
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "disconnected",
			RequestID: requestID,
		})
	}
}

func (s *controlSessionServer) finishValueRequest(requestID int64, value any,
	err error) {
	if err == nil {
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "value",
			RequestID: requestID,
			Value:     value,
		})
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "idle",
			RequestID: requestID,
		})
		s.sendResponse(controlSessionResponse{
			Type:   "response",
			ID:     requestID,
			OK:     true,
			Result: value,
		})
		return
	}

	if errors.Is(err, ErrInterrupted) {
		interruptErr := &helperError{Code: "interrupted", Message: err.Error()}
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "interrupted",
			RequestID: requestID,
		})
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "idle",
			RequestID: requestID,
		})
		s.sendResponse(controlSessionResponse{
			Type:  "response",
			ID:    requestID,
			OK:    false,
			Error: interruptErr,
		})
		return
	}

	responseErr := helperErrorFrom(err)
	s.sendEvent(controlSessionEvent{
		Type:      "event",
		Event:     "error",
		RequestID: requestID,
		Error:     responseErr,
	})
	s.sendEvent(controlSessionEvent{
		Type:      "event",
		Event:     "idle",
		RequestID: requestID,
	})
	s.sendResponse(controlSessionResponse{
		Type:  "response",
		ID:    requestID,
		OK:    false,
		Error: responseErr,
	})
	if !s.backend.IsConnected() {
		s.sendEvent(controlSessionEvent{
			Type:      "event",
			Event:     "disconnected",
			RequestID: requestID,
		})
	}
}

func (s *controlSessionServer) sendResponse(response controlSessionResponse) {
	s.writeJSON(response)
}

func (s *controlSessionServer) sendEvent(event controlSessionEvent) {
	s.writeJSON(event)
}

func (s *controlSessionServer) writeJSON(value any) {
	line, err := json.Marshal(value)
	if err != nil {
		return
	}

	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, _ = s.writer.Write(append(line, '\n'))
}

func helperErrorFrom(err error) *helperError {
	if err == nil {
		return nil
	}

	var connectErr *ConnectSelectionError
	if errors.As(err, &connectErr) {
		return &helperError{
			Code:       connectErr.Code,
			Message:    connectErr.Error(),
			Candidates: connectErr.Candidates,
		}
	}

	var controlErr *ControlError
	if errors.As(err, &controlErr) {
		return &helperError{
			Code:       "control_error",
			Message:    controlErr.Detail,
			Phase:      controlErr.Phase,
			DetailCode: controlErr.Code,
		}
	}

	if errors.Is(err, ErrNotConnected) {
		return &helperError{Code: "not_connected", Message: err.Error()}
	}
	if errors.Is(err, ErrResetUnavailable) {
		return &helperError{Code: "reset_unavailable", Message: err.Error()}
	}

	return &helperError{Code: "internal", Message: err.Error()}
}

func resetValueFrom(result *baseproto.ResetResponse) any {
	if result == nil {
		return nil
	}
	return resetValue{
		Status:           result.Status,
		HeapSize:         result.HeapSize,
		HeapUsed:         result.HeapUsed,
		HeapOverlayUsed:  result.HeapOverlayUsed,
		SlotCount:        result.SlotCount,
		SlotOverlayCount: result.SlotOverlayCount,
		Flags:            result.Flags,
		Version:          result.Version,
	}
}
