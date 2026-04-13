package cmd

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestLocalBinaryNeedsBuildWhenBinaryMissing(t *testing.T) {
	resetCommandGlobals(t)

	kernelRoot := makeLocalBuildKernelRoot(t)
	needsBuild, err := localBinaryNeedsBuild(filepath.Join(t.TempDir(), "Frothy"), kernelRoot)
	if err != nil {
		t.Fatalf("localBinaryNeedsBuild: %v", err)
	}
	if !needsBuild {
		t.Fatal("needsBuild = false, want true when binary is missing")
	}
}

func TestLocalBinaryNeedsBuildFalseWhenBinaryNewerThanSources(t *testing.T) {
	resetCommandGlobals(t)

	kernelRoot := makeLocalBuildKernelRoot(t)
	binaryPath := filepath.Join(t.TempDir(), "Frothy")
	mustWriteFile(t, binaryPath, "binary")

	base := time.Now().Add(-2 * time.Hour)
	setTreeModTime(t, kernelRoot, base)
	setTreeModTime(t, binaryPath, base.Add(time.Hour))

	needsBuild, err := localBinaryNeedsBuild(binaryPath, kernelRoot)
	if err != nil {
		t.Fatalf("localBinaryNeedsBuild: %v", err)
	}
	if needsBuild {
		t.Fatal("needsBuild = true, want false when binary is newer")
	}
}

func TestLocalBinaryNeedsBuildTrueWhenSourceNewerThanBinary(t *testing.T) {
	resetCommandGlobals(t)

	kernelRoot := makeLocalBuildKernelRoot(t)
	binaryPath := filepath.Join(t.TempDir(), "Frothy")
	mustWriteFile(t, binaryPath, "binary")

	base := time.Now().Add(-2 * time.Hour)
	setTreeModTime(t, kernelRoot, base)
	setTreeModTime(t, binaryPath, base)
	setTreeModTime(t, filepath.Join(kernelRoot, "src", "frothy_main.c"), base.Add(time.Hour))

	needsBuild, err := localBinaryNeedsBuild(binaryPath, kernelRoot)
	if err != nil {
		t.Fatalf("localBinaryNeedsBuild: %v", err)
	}
	if !needsBuild {
		t.Fatal("needsBuild = false, want true when a source is newer")
	}
}

func TestRunConnectLocalRejectsPortFlag(t *testing.T) {
	resetCommandGlobals(t)

	portFlag = "/dev/cu.usbserial-1234"
	err := runConnect([]string{"--local"})
	if err == nil {
		t.Fatal("runConnect succeeded, want error")
	}
	if err.Error() != "--local cannot be combined with --port" {
		t.Fatalf("runConnect error = %v, want local/port rejection", err)
	}
}

func TestBuildLocalConnectBinaryFallsBackToGMake(t *testing.T) {
	resetCommandGlobals(t)

	buildDir := t.TempDir()
	kernelRoot := makeLocalBuildKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "cmake"), "#!/bin/sh\nprintf 'cmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(toolsDir, "gmake"), "#!/bin/sh\nprintf 'gmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	prependPath(t, toolsDir)

	oldLookPath := connectLookPath
	connectLookPath = func(name string) (string, error) {
		if name == "make" {
			return "", exec.ErrNotFound
		}
		return exec.LookPath(name)
	}
	t.Cleanup(func() { connectLookPath = oldLookPath })

	if err := buildLocalConnectBinary(buildDir, kernelRoot); err != nil {
		t.Fatalf("buildLocalConnectBinary: %v", err)
	}

	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "gmake") {
		t.Fatalf("build log = %q, want gmake invocation", log)
	}
}

func makeLocalBuildKernelRoot(t *testing.T) string {
	t.Helper()

	root := t.TempDir()
	mustWriteFile(t, filepath.Join(root, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.23)\n")
	mustWriteFile(t, filepath.Join(root, "boards", "posix", "ffi.c"), "/* board */\n")
	mustWriteFile(t, filepath.Join(root, "cmake", "embed_froth.cmake"), "# embed\n")
	mustWriteFile(t, filepath.Join(root, "platforms", "posix", "platform.c"), "/* platform */\n")
	mustWriteFile(t, filepath.Join(root, "src", "froth_vm.h"), "/* vm */\n")
	mustWriteFile(t, filepath.Join(root, "src", "frothy_main.c"), "int main(void) { return 0; }\n")
	return root
}

func setTreeModTime(t *testing.T, path string, modTime time.Time) {
	t.Helper()

	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("stat %s: %v", path, err)
	}
	if info.IsDir() {
		err = filepath.Walk(path, func(walkPath string, walkInfo os.FileInfo, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			return os.Chtimes(walkPath, modTime, modTime)
		})
		if err != nil {
			t.Fatalf("chtimes %s: %v", path, err)
		}
		return
	}
	if err := os.Chtimes(path, modTime, modTime); err != nil {
		t.Fatalf("chtimes %s: %v", path, err)
	}
}
