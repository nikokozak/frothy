package frothycontrol

import (
	"errors"
	"fmt"
	"sync"

	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
	"github.com/nikokozak/froth/tools/cli/internal/serial"
)

var openSerialTransport = serial.Open
var openSerialTransportAndProbe = serial.OpenAndProbe

var ErrNotConnected = errors.New("not connected")
var ErrResetUnavailable = errors.New(
	"connected Frothy kernel does not support control reset",
)

type DeviceInfo struct {
	Port       string `json:"port"`
	Board      string `json:"board"`
	Version    string `json:"version"`
	CellBits   uint8  `json:"cell_bits"`
	MaxPayload uint16 `json:"max_payload,omitempty"`
	HeapSize   uint32 `json:"heap_size,omitempty"`
	HeapUsed   uint32 `json:"heap_used,omitempty"`
	SlotCount  uint16 `json:"slot_count,omitempty"`
}

type ConnectCandidate struct {
	Port    string `json:"port"`
	Board   string `json:"board"`
	Version string `json:"version"`
}

type ConnectSelectionError struct {
	Code       string
	Candidates []ConnectCandidate
	Err        error
}

func (e *ConnectSelectionError) Error() string {
	switch e.Code {
	case "multiple_devices":
		return "multiple Frothy devices found"
	case "no_devices":
		if e.Err != nil {
			return fmt.Sprintf("no Frothy device found: %v", e.Err)
		}
		return "no Frothy device found"
	default:
		if e.Err != nil {
			return e.Err.Error()
		}
		return "connect failed"
	}
}

func (e *ConnectSelectionError) Unwrap() error {
	return e.Err
}

type ManagerConfig struct {
	DefaultPort      string
	LocalRuntimePath string
	LocalRuntimeDir  string
}

type managedConnection struct {
	transport serial.Transport
	session   *Session
	info      *baseproto.HelloResponse
	port      string
	runtime   *LocalRuntime
}

type Manager struct {
	mu   sync.Mutex
	opMu sync.Mutex

	config ManagerConfig
	conn   *managedConnection
}

func NewManager(config ManagerConfig) *Manager {
	return &Manager{config: config}
}

func (m *Manager) IsConnected() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.conn != nil
}

func (m *Manager) Connect(portHint string) (*DeviceInfo, error) {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	conn := m.connection()
	targetPort := m.resolveTargetPort(portHint)
	if conn != nil {
		if targetPort == "" || conn.port == targetPort {
			return deviceInfoFromConnection(conn), nil
		}

		nextConn, err := m.openConnection(targetPort)
		if err != nil {
			return nil, err
		}
		m.mu.Lock()
		m.conn = nextConn
		m.mu.Unlock()
		if err := closeManagedConnection(conn, true); err != nil {
			return nil, err
		}
		return deviceInfoFromConnection(nextConn), nil
	}

	conn, err := m.openConnection(targetPort)
	if err != nil {
		return nil, err
	}

	m.mu.Lock()
	m.conn = conn
	m.mu.Unlock()
	return deviceInfoFromConnection(conn), nil
}

func (m *Manager) Disconnect() error {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	return m.closeConnection(true)
}

func (m *Manager) Eval(source string, onOutput func([]byte)) (string, error) {
	return m.runTextOp(func(session *Session) (string, error) {
		return session.Eval(source, 0, onOutput)
	})
}

func (m *Manager) Reset() (*baseproto.ResetResponse, error) {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	conn := m.connection()
	if conn == nil {
		return nil, ErrNotConnected
	}

	result, err := conn.session.Reset(controlCommandTimeout)
	if isResetUnavailableControlError(err) {
		return nil, ErrResetUnavailable
	}
	if isFatalSessionError(err) {
		_ = m.closeConnection(false)
	}
	return result, err
}

func (m *Manager) Words() ([]string, error) {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	conn := m.connection()
	if conn == nil {
		return nil, ErrNotConnected
	}

	names, err := conn.session.Words(controlCommandTimeout)
	if isFatalSessionError(err) {
		_ = m.closeConnection(false)
	}
	return names, err
}

func (m *Manager) See(name string) (*SeeResult, error) {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	conn := m.connection()
	if conn == nil {
		return nil, ErrNotConnected
	}

	view, err := conn.session.See(name, controlCommandTimeout)
	if isFatalSessionError(err) {
		_ = m.closeConnection(false)
	}
	return view, err
}

func (m *Manager) Save(onOutput func([]byte)) (string, error) {
	return m.runBuiltinCompat(func(session *Session) (string, error) {
		return session.Save(controlCommandTimeout, onOutput)
	}, "save:", onOutput)
}

func (m *Manager) Restore(onOutput func([]byte)) (string, error) {
	return m.runBuiltinCompat(func(session *Session) (string, error) {
		return session.Restore(controlCommandTimeout, onOutput)
	}, "restore:", onOutput)
}

func (m *Manager) Wipe(onOutput func([]byte)) (string, error) {
	return m.runBuiltinCompat(func(session *Session) (string, error) {
		return session.Wipe(controlCommandTimeout, onOutput)
	}, "dangerous.wipe:", onOutput)
}

func (m *Manager) Core(name string, onOutput func([]byte)) (string, error) {
	return m.runBuiltinCompat(func(session *Session) (string, error) {
		return session.Core(name, controlCommandTimeout, onOutput)
	}, fmt.Sprintf("core: @%s", name), onOutput)
}

func (m *Manager) SlotInfo(name string, onOutput func([]byte)) (string, error) {
	return m.runBuiltinCompat(func(session *Session) (string, error) {
		return session.SlotInfo(name, controlCommandTimeout, onOutput)
	}, fmt.Sprintf("slotInfo: @%s", name), onOutput)
}

func (m *Manager) Interrupt() error {
	conn := m.connection()
	if conn == nil {
		return ErrNotConnected
	}

	var err error
	if conn.runtime != nil {
		err = conn.runtime.Interrupt()
	} else {
		err = conn.session.Interrupt()
	}
	if isFatalSessionError(err) {
		_ = m.closeConnection(false)
	}
	return err
}

func (m *Manager) evalBuiltin(source string, onOutput func([]byte)) (string, error) {
	return m.runTextOp(func(session *Session) (string, error) {
		return session.Eval(source, 0, onOutput)
	})
}

func (m *Manager) runBuiltinCompat(runDirect func(*Session) (string, error),
	fallbackSource string, onOutput func([]byte)) (string, error) {
	return m.runTextOp(func(session *Session) (string, error) {
		value, err := runDirect(session)
		if isUnknownRequestControlError(err) {
			return session.Eval(fallbackSource, 0, onOutput)
		}
		return value, err
	})
}

func (m *Manager) runTextOp(run func(*Session) (string, error)) (string, error) {
	m.opMu.Lock()
	defer m.opMu.Unlock()

	conn := m.connection()
	if conn == nil {
		return "", ErrNotConnected
	}

	value, err := run(conn.session)
	if isFatalSessionError(err) {
		_ = m.closeConnection(false)
	}
	return value, err
}

func (m *Manager) currentDevice() *DeviceInfo {
	m.mu.Lock()
	defer m.mu.Unlock()
	return deviceInfoFromConnection(m.conn)
}

func (m *Manager) connection() *managedConnection {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.conn
}

func (m *Manager) openConnection(portHint string) (*managedConnection, error) {
	if m.config.LocalRuntimePath != "" {
		return m.openLocalRuntime()
	}

	targetPort := m.resolveTargetPort(portHint)
	if targetPort != "" {
		return m.openKnownSerialPort(targetPort)
	}
	return m.discoverUniqueSerial()
}

func (m *Manager) resolveTargetPort(portHint string) string {
	if portHint != "" {
		return portHint
	}
	return m.config.DefaultPort
}

func (m *Manager) openLocalRuntime() (*managedConnection, error) {
	var (
		runtime *LocalRuntime
		err     error
	)
	if m.config.LocalRuntimeDir != "" {
		runtime, err = StartLocalRuntimeInDir(
			m.config.LocalRuntimePath,
			m.config.LocalRuntimeDir,
			false,
		)
	} else {
		runtime, err = StartLocalRuntime(m.config.LocalRuntimePath)
	}
	if err != nil {
		return nil, err
	}

	conn, err := m.bindControlSession(runtime.Transport(), "stdin/stdout", runtime)
	if err != nil {
		_ = runtime.Close()
		return nil, err
	}
	return conn, nil
}

func (m *Manager) openKnownSerialPort(path string) (*managedConnection, error) {
	transport, err := openSerialTransport(path)
	if err != nil {
		return nil, err
	}

	conn, err := m.bindControlSession(transport, path, nil)
	if err != nil {
		_ = transport.Close()
		return nil, err
	}
	return conn, nil
}

func (m *Manager) openProbedSerialPort(path string) (*managedConnection, error) {
	transport, _, err := openSerialTransportAndProbe(path)
	if err != nil {
		return nil, err
	}

	conn, err := m.bindControlSession(transport, path, nil)
	if err != nil {
		_ = transport.Close()
		return nil, err
	}
	return conn, nil
}

func (m *Manager) discoverUniqueSerial() (*managedConnection, error) {
	candidates, err := serial.ListCandidates()
	if err != nil {
		return nil, err
	}
	if len(candidates) == 0 {
		return nil, &ConnectSelectionError{Code: "no_devices"}
	}

	var matches []*managedConnection
	var summaries []ConnectCandidate
	var lastErr error

	for _, path := range candidates {
		conn, err := m.openProbedSerialPort(path)
		if err != nil {
			lastErr = err
			continue
		}
		matches = append(matches, conn)
		summaries = append(summaries, ConnectCandidate{
			Port:    path,
			Board:   conn.info.Board,
			Version: conn.info.Version,
		})
	}

	switch len(matches) {
	case 0:
		return nil, &ConnectSelectionError{
			Code: "no_devices",
			Err:  lastErr,
		}
	case 1:
		return matches[0], nil
	default:
		for _, conn := range matches {
			_ = closeManagedConnection(conn, true)
		}
		return nil, &ConnectSelectionError{
			Code:       "multiple_devices",
			Candidates: summaries,
		}
	}
}

func (m *Manager) bindControlSession(transport serial.Transport, port string,
	runtime *LocalRuntime) (*managedConnection, error) {
	session := NewSession(transport)
	if err := session.AcquirePrompt(rawPromptTimeout); err != nil {
		return nil, fmt.Errorf("acquire prompt: %w", err)
	}
	if err := session.EnterControl(rawPromptTimeout); err != nil {
		return nil, fmt.Errorf("enter control: %w", err)
	}

	info, err := session.Hello(controlCommandTimeout)
	if err != nil {
		return nil, fmt.Errorf("HELLO: %w", err)
	}

	return &managedConnection{
		transport: transport,
		session:   session,
		info:      info,
		port:      port,
		runtime:   runtime,
	}, nil
}

func (m *Manager) closeConnection(detach bool) error {
	m.mu.Lock()
	conn := m.conn
	m.conn = nil
	m.mu.Unlock()

	return closeManagedConnection(conn, detach)
}

func closeManagedConnection(conn *managedConnection, detach bool) error {
	if conn == nil {
		return nil
	}

	var firstErr error
	if detach && conn.session != nil {
		if err := conn.session.Detach(controlCommandTimeout); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	if conn.runtime != nil {
		if err := conn.runtime.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		return firstErr
	}
	if conn.transport != nil {
		if err := conn.transport.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

func deviceInfoFromConnection(conn *managedConnection) *DeviceInfo {
	if conn == nil || conn.info == nil {
		return nil
	}
	return &DeviceInfo{
		Port:       conn.port,
		Board:      conn.info.Board,
		Version:    conn.info.Version,
		CellBits:   conn.info.CellBits,
		MaxPayload: conn.info.MaxPayload,
		HeapSize:   conn.info.HeapSize,
		HeapUsed:   conn.info.HeapUsed,
		SlotCount:  conn.info.SlotCount,
	}
}

func isFatalSessionError(err error) bool {
	if err == nil {
		return false
	}
	if errors.Is(err, ErrInterrupted) || errors.Is(err, ErrNotConnected) {
		return false
	}

	var controlErr *ControlError
	return !errors.As(err, &controlErr)
}

func isResetUnavailableControlError(err error) bool {
	return isUnknownRequestControlError(err)
}

func isUnknownRequestControlError(err error) bool {
	var controlErr *ControlError

	if !errors.As(err, &controlErr) {
		return false
	}

	return controlErr.Phase == phaseControl &&
		controlErr.Code == 256
}
