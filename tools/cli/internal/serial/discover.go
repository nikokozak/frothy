package serial

import (
	"fmt"
	"regexp"

	"go.bug.st/serial"
)

var discoverPorts = serial.GetPortsList

// candidatePattern matches likely USB-serial ports on macOS and Linux.
// Prefers /dev/cu.* on macOS (avoids /dev/tty.* which can cause
// additional DTR toggles during discovery).
var candidatePattern = regexp.MustCompile(
	`^/dev/cu\.(usbserial|usbmodem|SLAB_USBtoUART|USB|ACM)` +
		`|^/dev/tty(USB|ACM)`,
)

// IsCandidate reports whether a port path matches the USB-serial pattern.
func IsCandidate(path string) bool {
	return candidatePattern.MatchString(path)
}

// ListCandidates returns all port paths matching the USB-serial pattern.
func ListCandidates() ([]string, error) {
	ports, err := discoverPorts()
	if err != nil {
		return nil, err
	}
	var result []string
	for _, p := range ports {
		if candidatePattern.MatchString(p) {
			result = append(result, p)
		}
	}
	return result, nil
}

// DiscoverPath returns the unique USB-serial candidate path to open.
func DiscoverPath() (string, error) {
	candidates, err := ListCandidates()
	if err != nil {
		return "", fmt.Errorf("enumerate serial ports: %w", err)
	}

	switch len(candidates) {
	case 0:
		return "", fmt.Errorf("no USB-serial ports found")
	case 1:
		return candidates[0], nil
	default:
		return "", fmt.Errorf("multiple candidate ports found; use --port <path>")
	}
}

// Discover selects and opens the unique USB-serial candidate path.
func Discover() (*Port, error) {
	path, err := DiscoverPath()
	if err != nil {
		return nil, err
	}
	return Open(path)
}
