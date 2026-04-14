package project

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestWorkshopStarterSourceFilesResolveIntoCompleteForms(t *testing.T) {
	root := t.TempDir()

	manifest := `[project]
name = "starter"
version = "0.1.0"
entry = "src/main.froth"

[target]
board = "esp32-devkit-v1"
platform = "esp-idf"
`
	if err := os.WriteFile(filepath.Join(root, ManifestFile), []byte(manifest), 0644); err != nil {
		t.Fatalf("write manifest: %v", err)
	}

	for relPath, content := range StarterSourceFiles("esp32-devkit-v1") {
		absPath := filepath.Join(root, relPath)
		if err := os.MkdirAll(filepath.Dir(absPath), 0755); err != nil {
			t.Fatalf("mkdir %s: %v", filepath.Dir(absPath), err)
		}
		if err := os.WriteFile(absPath, []byte(content), 0644); err != nil {
			t.Fatalf("write %s: %v", absPath, err)
		}
	}

	manifestData, projectRoot, err := Load(root)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}

	result, err := Resolve(manifestData, projectRoot)
	if err != nil {
		t.Fatalf("Resolve: %v", err)
	}
	if len(result.Warnings) != 0 {
		t.Fatalf("Resolve warnings = %v, want none", result.Warnings)
	}
	if len(result.Files) != 3 {
		t.Fatalf("resolved files = %v, want 3 starter files", result.Files)
	}

	runtimeSource := StripBoundaryMarkers(result.Source)
	forms, err := SplitTopLevelForms(runtimeSource)
	if err != nil {
		t.Fatalf("SplitTopLevelForms: %v", err)
	}
	if len(forms) < 6 {
		t.Fatalf("forms = %d, want multiple starter definitions", len(forms))
	}

	for _, needle := range []string{
		`to lesson.ready [`,
		`to lesson.animate with step [`,
		`\ #allow-toplevel`,
		`player is cells(2)`,
		`to game.capture [`,
		`to boot [`,
		`lesson.ready:;`,
		`game.reset:`,
	} {
		if !strings.Contains(runtimeSource, needle) {
			t.Fatalf("runtime source missing %q\n%s", needle, runtimeSource)
		}
	}
}
