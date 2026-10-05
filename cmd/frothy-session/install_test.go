package main

import (
	"bytes"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func fakeInstallFactory(dev *fakeDevice) installDeviceFactory {
	return func(port string, baud int) (sessionDevice, func(), error) {
		return dev, func() {}, nil
	}
}

func writeFrothyToml(t *testing.T, dir, board string) {
	t.Helper()
	manifest := "name = \"install-test\"\nboard = \"" + board + "\"\n"
	if err := os.WriteFile(filepath.Join(dir, "frothy.toml"), []byte(manifest), 0o644); err != nil {
		t.Fatalf("write frothy.toml: %v", err)
	}
}

func writeLibraryFr(t *testing.T, dir, board, content string) string {
	t.Helper()
	buildDir := filepath.Join(dir, ".frothy", "build", board)
	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		t.Fatalf("mkdir build dir: %v", err)
	}
	path := filepath.Join(buildDir, "library.fr")
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatalf("write library.fr: %v", err)
	}
	return path
}

func TestRunInstallSendsLibraryThenExitsZero(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1", "lib_word is fn [ 42 ]\n")

	dev := &fakeDevice{responses: []string{statusResponse32("device"), "ok\n", "ok\n", "ok\n"}}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 0 {
		t.Fatalf("expected exit 0, got %d (stderr=%q)", code, stderr.String())
	}
	// install-user returns the board to the user tier, so later definitions
	// are user words.
	want := []string{"status", "install-library", "lib_word is fn [ 42 ]", "install-user"}
	if len(dev.sent) != len(want) {
		t.Fatalf("sent %v, want %v", dev.sent, want)
	}
	for i, line := range want {
		if dev.sent[i] != line {
			t.Fatalf("sent[%d]=%q, want %q (full=%v)", i, dev.sent[i], line, dev.sent)
		}
	}
	if dev.syncs != 1 {
		t.Fatalf("expected one sync, got %d", dev.syncs)
	}
}

func TestRunInstallReportsMissingLibraryFr(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")

	dev := &fakeDevice{}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 1 {
		t.Fatalf("expected exit 1, got %d (stderr=%q)", code, stderr.String())
	}
	if !strings.Contains(stderr.String(), "error: library.fr not found at ") {
		t.Fatalf("expected missing-library message, got %q", stderr.String())
	}
	if !strings.Contains(stderr.String(), "run frothy build first") {
		t.Fatalf("expected hint to run frothy build, got %q", stderr.String())
	}
	if len(dev.sent) != 0 {
		t.Fatalf("expected no device traffic, got %v", dev.sent)
	}
}

func TestRunInstallSurfacesDeviceErrorMidPipe(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1",
		"lib_one is fn [ 1 ]\nlib_two is fn [ 2 ]\n")

	dev := &fakeDevice{responses: []string{statusResponse32("device"), "ok\n", "error: unsupported (9)\n", "ok\n"}}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 1 {
		t.Fatalf("expected exit 1, got %d (stderr=%q)", code, stderr.String())
	}
	if !strings.Contains(stderr.String(), "error: device returned error: unsupported (9)") {
		t.Fatalf("expected surfaced device error, got %q", stderr.String())
	}
	// No further library line after the error, but install-user returns the
	// board to the user tier: library mode must not outlive a failed install.
	wantSent := []string{"status", "install-library", "lib_one is fn [ 1 ]", "install-user"}
	if len(dev.sent) != len(wantSent) {
		t.Fatalf("sent %v, want %v", dev.sent, wantSent)
	}
	for i, line := range wantSent {
		if dev.sent[i] != line {
			t.Fatalf("sent[%d]=%q, want %q (full=%v)", i, dev.sent[i], line, dev.sent)
		}
	}
}

// The device can enter library mode before its answer to install-library
// reaches the host, so a timeout there also ends library mode.
func TestRunInstallEndsLibraryModeAfterUnansweredInstallLibrary(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1", "lib_word is fn [ 42 ]\n")

	dev := &fakeDevice{
		responses:    []string{statusResponse32("device"), "", "ok\n"},
		responseErrs: []error{nil, errPromptTimeout, nil},
	}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 1 {
		t.Fatalf("expected exit 1, got %d (stderr=%q)", code, stderr.String())
	}
	if got, want := strings.Join(dev.sent, "\n"), "status\ninstall-library\ninstall-user"; got != want {
		t.Fatalf("sent %q, want %q", got, want)
	}
}

func TestRunInstallReportsNoticeAndContinues(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1",
		"save\nlib_word is fn [ 42 ]\n")

	notice := "notice: not saved (13)\n" +
		"detail: cannot save slot 'appuart' - bound to a live handle or buffer\n" +
		"ok\n"
	dev := &fakeDevice{responses: []string{statusResponse32("device"), "ok\n", notice, "ok\n", "ok\n"}}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 0 {
		t.Fatalf("expected exit 0, got %d (stderr=%q)", code, stderr.String())
	}
	if got, want := stderr.String(),
		"notice: not saved (13)\n"+
			"detail: cannot save slot 'appuart' - bound to a live handle or buffer\n"; got != want {
		t.Fatalf("stderr = %q, want raw notice %q", got, want)
	}
	if got, want := strings.Join(dev.sent, "\n"),
		"status\ninstall-library\nsave\nlib_word is fn [ 42 ]\ninstall-user"; got != want {
		t.Fatalf("sent forms = %q, want %q", got, want)
	}
}

func TestRunInstallReportsOpenFailure(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1", "lib_word is fn [ 42 ]\n")

	openErr := errors.New("permission denied")
	factory := func(port string, baud int) (sessionDevice, func(), error) {
		return nil, nil, openErr
	}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), factory,
		115200, time.Second, 0,
	)
	if code != 1 {
		t.Fatalf("expected exit 1, got %d", code)
	}
	if !strings.Contains(stderr.String(), "error: cannot open /dev/cu.usbserial-0001: permission denied") {
		t.Fatalf("expected open-failure error, got %q", stderr.String())
	}
}

func TestRunInstallRejectsPositionalArgs(t *testing.T) {
	dev := &fakeDevice{}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"library", "--port", "/dev/cu.usbserial-0001"},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code != 2 {
		t.Fatalf("expected exit 2 for positional args, got %d", code)
	}
	if len(dev.sent) != 0 {
		t.Fatalf("expected no device traffic, got %v", dev.sent)
	}
}

// A library form longer than the device line stops the install before
// install-library, so the device never holds half a library.
func TestRunInstallRefusesFormLongerThanDeviceLine(t *testing.T) {
	projectDir := t.TempDir()
	writeFrothyToml(t, projectDir, "esp32_devkit_v1")
	writeLibraryFr(t, projectDir, "esp32_devkit_v1",
		"lib_word is fn [ 42 ]\nlib_long is fn [ "+strings.Repeat("42 ; ", 120)+"42 ]\n")
	status := strings.Replace(statusResponse32("device"), "apply_bytes=128",
		"apply_bytes=128 line_bytes=511", 1)
	dev := &fakeDevice{responses: []string{status}}
	var stderr bytes.Buffer

	code := runInstallCommand(
		[]string{"--port", "/dev/cu.usbserial-0001", "--project", projectDir},
		io.Discard, &stderr, singlePortLister("/dev/cu.usbserial-0001"), fakeInstallFactory(dev),
		115200, time.Second, 0,
	)
	if code == 0 || !strings.Contains(stderr.String(), "the device reads at most 511 bytes in one line") {
		t.Fatalf("code = %d, stderr = %q; want a refusal that names the limit", code, stderr.String())
	}
	if got := strings.Join(dev.sent, "\n"); got != "status" {
		t.Fatalf("sent %q, want only the status read", got)
	}
}
