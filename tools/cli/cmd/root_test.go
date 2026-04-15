package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestExecutePrintsUsageWithNoCommand(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"frothy"}
	t.Cleanup(func() { os.Args = oldArgs })

	stdout, stderr := captureOutput(t, func() {
		if err := Execute(); err != nil {
			t.Fatalf("Execute: %v", err)
		}
	})

	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "Usage: frothy [flags] <command>") {
		t.Fatalf("stdout = %q, want usage", stdout)
	}
	if !strings.Contains(stdout, "--board <name>") {
		t.Fatalf("stdout = %q, want board flag help", stdout)
	}
}

func TestExecuteRejectsUnknownCommand(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"frothy", "definitely-not-a-command"}
	t.Cleanup(func() { os.Args = oldArgs })

	err := Execute()
	if err == nil {
		t.Fatal("Execute succeeded, want error")
	}
	if !strings.Contains(err.Error(), "unknown command: definitely-not-a-command") {
		t.Fatalf("Execute error = %v, want unknown command", err)
	}
}

func TestExecutePrintsVersion(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"frothy", "--version"}
	t.Cleanup(func() { os.Args = oldArgs })

	stdout, stderr := captureOutput(t, func() {
		if err := Execute(); err != nil {
			t.Fatalf("Execute: %v", err)
		}
	})

	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	want := "frothy " + frothVersion(t)
	if !strings.Contains(stdout, want) {
		t.Fatalf("stdout = %q, want %q", stdout, want)
	}
}

func TestExecuteDispatchesSetup(t *testing.T) {
	resetCommandGlobals(t)

	oldArgs := os.Args
	os.Args = []string{"frothy", "setup", "esp-idf", "--force"}
	t.Cleanup(func() { os.Args = oldArgs })

	var got []string
	runSetupCommand = func(args []string) error {
		got = append([]string(nil), args...)
		return nil
	}

	if err := Execute(); err != nil {
		t.Fatalf("Execute: %v", err)
	}

	want := []string{"esp-idf", "--force"}
	if strings.Join(got, " ") != strings.Join(want, " ") {
		t.Fatalf("setup args = %v, want %v", got, want)
	}
}

func TestExecuteDispatchesToolingResolveSource(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	filePath := filepath.Join(projectRoot, "main.froth")
	mustWriteFile(t, filePath, "boot { 42 }\n")
	withChdir(t, projectRoot)

	oldArgs := os.Args
	os.Args = []string{"frothy", "tooling", "resolve-source", filePath}
	t.Cleanup(func() { os.Args = oldArgs })

	stdout, stderr := captureOutput(t, func() {
		if err := Execute(); err != nil {
			t.Fatalf("Execute: %v", err)
		}
	})

	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "boot { 42 }") {
		t.Fatalf("stdout = %q, want source content", stdout)
	}
	if strings.Contains(stdout, "autorun") {
		t.Fatalf("stdout = %q, want Frothy runtime source without autorun wrapper", stdout)
	}
}
