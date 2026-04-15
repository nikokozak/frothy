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

func TestRunFlashManifestOverridesCLISelectionWithNote(t *testing.T) {
	resetCommandGlobals(t)
	projectRoot := t.TempDir()
	targetFlag = "esp-idf"
	boardFlag = "esp32-devkit-v4-game-board"
	withChdir(t, projectRoot)
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	oldBuildManifest := flashBuildManifest
	flashBuildManifest = func(*project.Manifest, string) error { return nil }
	t.Cleanup(func() { flashBuildManifest = oldBuildManifest })

	stdout, stderr := captureOutput(t, func() {
		if err := runFlash(); err != nil {
			t.Fatalf("runFlash: %v", err)
		}
	})

	if !strings.Contains(stderr, "ignoring --target esp-idf --board esp32-devkit-v4-game-board because froth.toml selects posix (posix)") {
		t.Fatalf("stderr = %q, want manifest override note", stderr)
	}
	if !strings.Contains(stdout, "Building for posix...") {
		t.Fatalf("stdout = %q, want manifest flash build note", stdout)
	}
	if !strings.Contains(stdout, "binary: ") {
		t.Fatalf("stdout = %q, want posix binary output", stdout)
	}
}

func TestRunFlashLegacyBoardFlagBuildsBeforeFlashing(t *testing.T) {
	resetCommandGlobals(t)
	targetFlag = "esp-idf"
	boardFlag = "esp32-devkit-v4-game-board"
	portFlag = "/dev/cu.usbserial-test"

	root := makeFakeKernelRoot(t)
	targetDir := filepath.Join(root, "targets", "esp-idf")
	mustWriteFile(t, filepath.Join(targetDir, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(froth)\n")
	mustWriteFile(t, filepath.Join(targetDir, "build", "stale.txt"), "stale")

	logPath := filepath.Join(t.TempDir(), "idf.log")
	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "idf.py"), "#!/bin/sh\nprintf 'idf.py %s\\n' \"$*\" >> \""+logPath+"\"\nmkdir -p build\n: > build/rebuilt.txt\n")

	frothyHome := t.TempDir()
	exportPath := filepath.Join(frothyHome, "sdk", "esp-idf", "export.sh")
	mustWriteFile(t, exportPath, "export PATH=\""+toolsDir+":$PATH\"\n")
	t.Setenv("FROTHY_HOME", frothyHome)

	withChdir(t, root)

	_, stderr := captureOutput(t, func() {
		if err := runFlashLegacy(); err != nil {
			t.Fatalf("runFlashLegacy: %v", err)
		}
	})

	if _, err := os.Stat(filepath.Join(targetDir, "build", "stale.txt")); !os.IsNotExist(err) {
		t.Fatalf("stale file still exists after explicit board flash: %v", err)
	}
	log := mustReadFile(t, logPath)
	buildIndex := strings.Index(log, "-DFROTH_BOARD=esp32-devkit-v4-game-board")
	flashIndex := strings.LastIndex(log, "flash -p /dev/cu.usbserial-test")
	if buildIndex == -1 {
		t.Fatalf("idf log = %q, want board-selected build", log)
	}
	if flashIndex == -1 {
		t.Fatalf("idf log = %q, want flash step", log)
	}
	if flashIndex <= buildIndex {
		t.Fatalf("idf log = %q, want build before flash", log)
	}
	if !strings.Contains(stderr, "cleaned build directory to avoid sticky target/board cache for --target esp-idf --board esp32-devkit-v4-game-board") {
		t.Fatalf("stderr = %q, want explicit-selection cache note", stderr)
	}
}
