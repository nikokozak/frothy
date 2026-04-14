package serial

import (
	"fmt"
	"testing"
)

func TestDiscoverPathReturnsOnlyCandidate(t *testing.T) {
	oldDiscoverPorts := discoverPorts
	defer func() {
		discoverPorts = oldDiscoverPorts
	}()

	discoverPorts = func() ([]string, error) {
		return []string{
			"/dev/cu.usbserial-0001",
			"/dev/cu.Bluetooth-Incoming-Port",
		}, nil
	}

	path, err := DiscoverPath()
	if err != nil {
		t.Fatalf("DiscoverPath failed: %v", err)
	}
	if path != "/dev/cu.usbserial-0001" {
		t.Fatalf("unexpected port path: %s", path)
	}
}

func TestDiscoverPathRejectsMultipleCandidates(t *testing.T) {
	oldDiscoverPorts := discoverPorts
	defer func() {
		discoverPorts = oldDiscoverPorts
	}()

	discoverPorts = func() ([]string, error) {
		return []string{
			"/dev/cu.usbserial-0001",
			"/dev/cu.usbmodem-0001",
		}, nil
	}

	_, err := DiscoverPath()
	if err == nil {
		t.Fatal("expected DiscoverPath error")
	}
	if err.Error() != "multiple candidate ports found; use --port <path>" {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestDiscoverPathWrapsEnumerationErrors(t *testing.T) {
	oldDiscoverPorts := discoverPorts
	defer func() {
		discoverPorts = oldDiscoverPorts
	}()

	root := fmt.Errorf("enumerator broke")
	discoverPorts = func() ([]string, error) {
		return nil, root
	}

	_, err := DiscoverPath()
	if err == nil {
		t.Fatal("expected DiscoverPath error")
	}
	if err.Error() != "enumerate serial ports: enumerator broke" {
		t.Fatalf("unexpected error: %v", err)
	}
}
