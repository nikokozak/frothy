package serial

import (
	"errors"
	"fmt"
	"testing"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/protocol"
)

func TestOpenAndProbeRetriesFreshOpen(t *testing.T) {
	oldSleep := discoverSleep
	oldOpenAndProbePath := openAndProbePath
	oldOpenAndProbeOnce := openAndProbeOnce
	defer func() {
		discoverSleep = oldSleep
		openAndProbePath = oldOpenAndProbePath
		openAndProbeOnce = oldOpenAndProbeOnce
	}()

	discoverSleep = func(time.Duration) {}
	attempts := 0
	openAndProbeOnce = func(path string) (*Port, *protocol.HelloResponse, error) {
		attempts++
		if attempts == 1 {
			return nil, nil, fmt.Errorf("handshake failed")
		}
		return &Port{path: path}, &protocol.HelloResponse{
			Version:  "0.1.0-test",
			Board:    "mock-board",
			CellBits: 32,
		}, nil
	}

	port, hello, err := OpenAndProbe("/dev/cu.usbserial-0001")
	if err != nil {
		t.Fatalf("OpenAndProbe failed: %v", err)
	}
	if attempts != 2 {
		t.Fatalf("expected 2 attempts, got %d", attempts)
	}
	if port.Path() != "/dev/cu.usbserial-0001" {
		t.Fatalf("unexpected port path: %s", port.Path())
	}
	if hello.Board != "mock-board" {
		t.Fatalf("unexpected board: %s", hello.Board)
	}
}

func TestDiscoverUsesOpenAndProbePath(t *testing.T) {
	oldDiscoverPorts := discoverPorts
	oldOpenAndProbePath := openAndProbePath
	defer func() {
		discoverPorts = oldDiscoverPorts
		openAndProbePath = oldOpenAndProbePath
	}()

	discoverPorts = func() ([]string, error) {
		return []string{
			"/dev/cu.usbserial-0001",
			"/dev/cu.Bluetooth-Incoming-Port",
		}, nil
	}

	openAndProbePath = func(path string) (*Port, *protocol.HelloResponse, error) {
		return &Port{path: path}, &protocol.HelloResponse{
			Version:  "0.1.0-test",
			Board:    "mock-board",
			CellBits: 32,
		}, nil
	}

	port, hello, err := Discover()
	if err != nil {
		t.Fatalf("Discover failed: %v", err)
	}
	if port.Path() != "/dev/cu.usbserial-0001" {
		t.Fatalf("unexpected port path: %s", port.Path())
	}
	if hello.Version != "0.1.0-test" {
		t.Fatalf("unexpected version: %s", hello.Version)
	}
}

func TestDiscoverReturnsLastProbeDiagnostic(t *testing.T) {
	oldDiscoverPorts := discoverPorts
	oldOpenAndProbePath := openAndProbePath
	defer func() {
		discoverPorts = oldDiscoverPorts
		openAndProbePath = oldOpenAndProbePath
	}()

	discoverPorts = func() ([]string, error) {
		return []string{"/dev/cu.usbserial-0001"}, nil
	}

	root := fmt.Errorf("no HELLO_RES after 3 attempts")
	openAndProbePath = func(path string) (*Port, *protocol.HelloResponse, error) {
		return nil, nil, root
	}

	_, _, err := Discover()
	if err == nil {
		t.Fatal("expected discover error")
	}

	var discoverErr *DiscoverError
	if !errors.As(err, &discoverErr) {
		t.Fatalf("expected DiscoverError, got %T", err)
	}
	if discoverErr.Path != "/dev/cu.usbserial-0001" {
		t.Fatalf("unexpected path: %s", discoverErr.Path)
	}
	if !errors.Is(err, root) {
		t.Fatalf("expected wrapped root error, got %v", err)
	}
	if err.Error() != "no Froth device found" {
		t.Fatalf("unexpected public error text: %q", err.Error())
	}
}
