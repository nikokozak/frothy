package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/frothy/tools/cli/internal/project"
)

func TestRunNewCreatesProjectSkeletonWithDefaultPosixTarget(t *testing.T) {
	resetCommandGlobals(t)

	projectDir := filepath.Join(t.TempDir(), "myproject")
	stdout, stderr := captureOutput(t, func() {
		if err := runNew([]string{projectDir}); err != nil {
			t.Fatalf("runNew: %v", err)
		}
	})

	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "Created project myproject") {
		t.Fatalf("stdout = %q, want project creation message", stdout)
	}

	for _, rel := range []string{
		"froth.toml",
		filepath.Join("src", "main.froth"),
		filepath.Join("lib", ".gitkeep"),
		".gitignore",
	} {
		path := filepath.Join(projectDir, rel)
		if _, err := os.Stat(path); err != nil {
			t.Fatalf("expected %s to exist: %v", path, err)
		}
	}

	manifest := mustReadFile(t, filepath.Join(projectDir, "froth.toml"))
	if !strings.Contains(manifest, `board = "posix"`) {
		t.Fatalf("manifest = %q, want default board posix", manifest)
	}
	if !strings.Contains(manifest, `platform = "posix"`) {
		t.Fatalf("manifest = %q, want default platform posix", manifest)
	}
	if !strings.Contains(manifest, `# [dependencies]`) {
		t.Fatalf("manifest = %q, want commented dependency example", manifest)
	}
	if !strings.Contains(manifest, `# \ #use "utils"`) {
		t.Fatalf("manifest = %q, want named include example", manifest)
	}
	if !strings.Contains(manifest, `# \ #use "../lib/utils.froth"`) {
		t.Fatalf("manifest = %q, want relative include example", manifest)
	}

	mainSource := mustReadFile(t, filepath.Join(projectDir, "src", "main.froth"))
	if !strings.Contains(mainSource, "boot {") {
		t.Fatalf("main.froth = %q, want boot block scaffold", mainSource)
	}
}

func TestRunNewSetsESP32PlatformFromTargetFlag(t *testing.T) {
	resetCommandGlobals(t)
	boardFlag = "esp32-devkit-v1"

	projectDir := filepath.Join(t.TempDir(), "blink")
	stdout, stderr := captureOutput(t, func() {
		if err := runNew([]string{projectDir}); err != nil {
			t.Fatalf("runNew: %v", err)
		}
	})
	if stderr != "" {
		t.Fatalf("stderr = %q, want empty", stderr)
	}
	if !strings.Contains(stdout, "frothy doctor") || !strings.Contains(stdout, "frothy flash") {
		t.Fatalf("stdout = %q, want ESP32 setup next steps", stdout)
	}

	manifest, root, err := project.Load(projectDir)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	resolved, err := project.Resolve(manifest, root)
	if err != nil {
		t.Fatalf("project.Resolve: %v", err)
	}
	if len(resolved.Warnings) != 0 {
		t.Fatalf("project.Resolve warnings = %v, want none", resolved.Warnings)
	}

	if _, err := os.Stat(filepath.Join(projectDir, "src", "workshop")); !os.IsNotExist(err) {
		t.Fatalf("workshop scaffold should not be created, stat err = %v", err)
	}

	mainSource := mustReadFile(t, filepath.Join(projectDir, "src", "main.froth"))
	if !strings.Contains(mainSource, "boot {") {
		t.Fatalf("main scaffold = %q, want default boot block", mainSource)
	}

	manifestText := mustReadFile(t, filepath.Join(projectDir, "froth.toml"))
	if !strings.Contains(manifestText, `board = "esp32-devkit-v1"`) {
		t.Fatalf("manifest = %q, want esp32 board", manifestText)
	}
	if !strings.Contains(manifestText, `platform = "esp-idf"`) {
		t.Fatalf("manifest = %q, want esp-idf platform", manifestText)
	}
}

func TestRunNewRejectsUnknownTargetFlag(t *testing.T) {
	resetCommandGlobals(t)
	boardFlag = "esp32-devkit-v1x"

	projectDir := filepath.Join(t.TempDir(), "blink")
	err := runNew([]string{projectDir})
	if err == nil {
		t.Fatal("runNew succeeded, want error")
	}
	if !strings.Contains(err.Error(), "unknown board") {
		t.Fatalf("runNew error = %v, want unknown board error", err)
	}
}

func TestRunNewRequiresBoardForESPIDFTarget(t *testing.T) {
	resetCommandGlobals(t)
	targetFlag = "esp-idf"

	projectDir := filepath.Join(t.TempDir(), "blink")
	err := runNew([]string{projectDir})
	if err == nil {
		t.Fatal("runNew succeeded, want error")
	}
	if !strings.Contains(err.Error(), "requires --board") {
		t.Fatalf("runNew error = %v, want board requirement", err)
	}
}

func TestRunNewAcceptsDeprecatedTargetBoardAliasWithNote(t *testing.T) {
	resetCommandGlobals(t)
	targetFlag = "esp32-devkit-v1"

	projectDir := filepath.Join(t.TempDir(), "blink")
	_, stderr := captureOutput(t, func() {
		if err := runNew([]string{projectDir}); err != nil {
			t.Fatalf("runNew: %v", err)
		}
	})

	if !strings.Contains(stderr, "frothy new --target <board>` is deprecated") {
		t.Fatalf("stderr = %q, want deprecation note", stderr)
	}
}

func TestRunNewRejectsInvalidProjectNames(t *testing.T) {
	resetCommandGlobals(t)

	cases := []string{
		`bad"name`,
		`bad\name`,
		"bad\nname",
	}

	for _, name := range cases {
		dir := filepath.Join(t.TempDir(), name)
		err := runNew([]string{dir})
		if err == nil {
			t.Fatalf("runNew(%q) succeeded, want error", dir)
		}
		if !strings.Contains(err.Error(), "invalid characters") {
			t.Fatalf("runNew(%q) error = %v, want invalid characters", dir, err)
		}
	}
}

func TestRunNewExtractsNameFromPathArgument(t *testing.T) {
	resetCommandGlobals(t)

	base := filepath.Join(t.TempDir(), strings.Repeat("deep-", 10), "myproject")
	if err := runNew([]string{base}); err != nil {
		t.Fatalf("runNew: %v", err)
	}

	manifest := mustReadFile(t, filepath.Join(base, "froth.toml"))
	if !strings.Contains(manifest, `name = "myproject"`) {
		t.Fatalf("manifest = %q, want project name myproject", manifest)
	}
}

func TestRunNewRejectsExistingDirectory(t *testing.T) {
	resetCommandGlobals(t)

	projectDir := filepath.Join(t.TempDir(), "existing")
	mustWriteFile(t, filepath.Join(projectDir, "placeholder.txt"), "x")

	err := runNew([]string{projectDir})
	if err == nil {
		t.Fatal("runNew succeeded, want error")
	}
	if !strings.Contains(err.Error(), "already exists") {
		t.Fatalf("runNew error = %v, want already exists", err)
	}
}

func TestRunNewRejectsEmptyName(t *testing.T) {
	resetCommandGlobals(t)

	err := runNew([]string{""})
	if err == nil {
		t.Fatal("runNew succeeded, want error")
	}
	if !strings.Contains(err.Error(), "empty") && !strings.Contains(err.Error(), "project name") {
		t.Fatalf("runNew error = %v, want empty-name error", err)
	}
}
