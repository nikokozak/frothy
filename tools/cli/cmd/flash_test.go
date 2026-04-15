package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/frothy/tools/cli/internal/project"
)

func TestRunFlashManifestReportsFrothyPosixBinary(t *testing.T) {
	resetCommandGlobals(t)

	root := t.TempDir()
	manifest := &project.Manifest{
		Target: project.TargetConfig{
			Board:    "posix",
			Platform: "posix",
		},
	}

	oldBuildManifest := flashBuildManifest
	flashBuildManifest = func(m *project.Manifest, gotRoot string) error {
		if gotRoot != root {
			t.Fatalf("build root = %q, want %q", gotRoot, root)
		}
		return nil
	}
	t.Cleanup(func() { flashBuildManifest = oldBuildManifest })

	stdout, _ := captureOutput(t, func() {
		if err := runFlashManifest(manifest, root); err != nil {
			t.Fatalf("runFlashManifest: %v", err)
		}
	})

	if !strings.Contains(stdout, filepath.Join(root, ".froth-build", "firmware", "Frothy")) {
		t.Fatalf("stdout = %q, want Frothy binary path", stdout)
	}
}

func TestRunFlashManifestAppliesRuntimeImageAfterESPIDFFlash(t *testing.T) {
	resetCommandGlobals(t)

	root := t.TempDir()
	manifest := &project.Manifest{
		Target: project.TargetConfig{
			Board:    "esp32-devkit-v1",
			Platform: "esp-idf",
		},
	}

	oldBuildManifest := flashBuildManifest
	oldResolvePort := flashResolvePort
	oldFlashDir := flashESPIDFDirFn
	oldApplyRuntime := flashApplyRuntime
	flashBuildManifest = func(*project.Manifest, string) error { return nil }
	flashResolvePort = func() (string, error) { return "/dev/cu.mock", nil }

	var flashedDir, flashedPort string
	flashESPIDFDirFn = func(dir string, port string) error {
		flashedDir = dir
		flashedPort = port
		return nil
	}

	var appliedPort, appliedRuntime string
	flashApplyRuntime = func(port string, runtimePath string) error {
		appliedPort = port
		appliedRuntime = runtimePath
		return nil
	}

	t.Cleanup(func() {
		flashBuildManifest = oldBuildManifest
		flashResolvePort = oldResolvePort
		flashESPIDFDirFn = oldFlashDir
		flashApplyRuntime = oldApplyRuntime
	})

	stdout, _ := captureOutput(t, func() {
		if err := runFlashManifest(manifest, root); err != nil {
			t.Fatalf("runFlashManifest: %v", err)
		}
	})

	if flashedDir != filepath.Join(root, ".froth-build", "esp-idf") {
		t.Fatalf("flash dir = %q", flashedDir)
	}
	if flashedPort != "/dev/cu.mock" {
		t.Fatalf("flash port = %q", flashedPort)
	}
	if appliedPort != "/dev/cu.mock" {
		t.Fatalf("apply port = %q", appliedPort)
	}
	if appliedRuntime != filepath.Join(root, ".froth-build", "runtime.frothy") {
		t.Fatalf("runtime path = %q", appliedRuntime)
	}
	if !strings.Contains(stdout, "Applying runtime source: "+filepath.Join(root, ".froth-build", "runtime.frothy")) {
		t.Fatalf("stdout = %q, want runtime apply message", stdout)
	}
}

func TestRunFlashPrefersLocalCheckout(t *testing.T) {
	resetCommandGlobals(t)

	repoRoot := t.TempDir()
	mustWriteFile(t, filepath.Join(repoRoot, "CMakeLists.txt"), "project(Froth)\n")
	mustWriteFile(t, filepath.Join(repoRoot, "src", "froth_vm.h"), "/* vm */\n")
	mustWriteFile(t, filepath.Join(repoRoot, "targets", "esp-idf", "CMakeLists.txt"), "project(froth)\n")
	if err := os.MkdirAll(filepath.Join(repoRoot, "nested"), 0755); err != nil {
		t.Fatalf("mkdir nested: %v", err)
	}
	withChdir(t, filepath.Join(repoRoot, "nested"))
	targetFlag = "esp-idf"
	portFlag = "/dev/cu.usbserial-test"
	t.Setenv("FROTHY_HOME", filepath.Join(t.TempDir(), "frothy-home"))

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "ESP-IDF not found") {
		t.Fatalf("error = %v, want local checkout flash path", err)
	}
}

func TestRunFlashOutsideProjectRequiresSourceBasedContext(t *testing.T) {
	resetCommandGlobals(t)

	withChdir(t, t.TempDir())

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "source-based flashing only") {
		t.Fatalf("error = %v, want source-based flashing guidance", err)
	}
}

func TestRunFlashRejectsLegacyTargetFlagInsideManifestProject(t *testing.T) {
	resetCommandGlobals(t)
	projectRoot := t.TempDir()
	targetFlag = "esp-idf"
	withChdir(t, projectRoot)
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "does not accept --target inside a project") {
		t.Fatalf("runFlash error = %v, want manifest target guidance", err)
	}
}
