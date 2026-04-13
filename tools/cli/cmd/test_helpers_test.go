package cmd

import (
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

func resetCommandGlobals(t *testing.T) {
	t.Helper()

	oldPort := portFlag
	oldTarget := targetFlag
	oldClean := cleanFlag
	oldRunSetup := runSetupCommand

	portFlag = ""
	targetFlag = ""
	cleanFlag = false
	runSetupCommand = runSetup

	t.Cleanup(func() {
		portFlag = oldPort
		targetFlag = oldTarget
		cleanFlag = oldClean
		runSetupCommand = oldRunSetup
	})
}

func withChdir(t *testing.T, dir string) {
	t.Helper()

	oldWD, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatalf("chdir %s: %v", dir, err)
	}
	t.Cleanup(func() {
		if err := os.Chdir(oldWD); err != nil {
			t.Fatalf("restore cwd: %v", err)
		}
	})
}

func captureOutput(t *testing.T, fn func()) (string, string) {
	t.Helper()

	oldStdout := os.Stdout
	oldStderr := os.Stderr

	stdoutR, stdoutW, err := os.Pipe()
	if err != nil {
		t.Fatalf("stdout pipe: %v", err)
	}
	stderrR, stderrW, err := os.Pipe()
	if err != nil {
		t.Fatalf("stderr pipe: %v", err)
	}

	os.Stdout = stdoutW
	os.Stderr = stderrW

	var stdoutBuf strings.Builder
	var stderrBuf strings.Builder
	var wg sync.WaitGroup
	wg.Add(2)

	go func() {
		defer wg.Done()
		_, _ = io.Copy(&stdoutBuf, stdoutR)
	}()
	go func() {
		defer wg.Done()
		_, _ = io.Copy(&stderrBuf, stderrR)
	}()

	fn()

	_ = stdoutW.Close()
	_ = stderrW.Close()
	os.Stdout = oldStdout
	os.Stderr = oldStderr
	wg.Wait()

	return stdoutBuf.String(), stderrBuf.String()
}

func frothVersion(t *testing.T) string {
	t.Helper()

	version, err := os.ReadFile(filepath.Join(repoRoot(t), "VERSION"))
	if err != nil {
		t.Fatalf("read VERSION: %v", err)
	}
	return strings.TrimSpace(string(version))
}

func repoRoot(t *testing.T) string {
	t.Helper()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	return filepath.Clean(filepath.Join(wd, "..", "..", ".."))
}

func mustWriteFile(t *testing.T, path string, content string) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func mustWriteExecutable(t *testing.T, path string, content string) {
	t.Helper()

	mustWriteFile(t, path, content)
	if err := os.Chmod(path, 0755); err != nil {
		t.Fatalf("chmod %s: %v", path, err)
	}
}

func prependPath(t *testing.T, dir string) {
	t.Helper()

	oldPath := os.Getenv("PATH")
	t.Setenv("PATH", dir+string(os.PathListSeparator)+oldPath)
}

func writeFakeBuildTools(t *testing.T, dir string, logPath string) {
	t.Helper()

	mustWriteExecutable(t, filepath.Join(dir, "cmake"), "#!/bin/sh\nprintf 'cmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(dir, "make"), "#!/bin/sh\nprintf 'make %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
}

func mustReadFile(t *testing.T, path string) string {
	t.Helper()

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(data)
}
