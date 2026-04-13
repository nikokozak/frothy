package project

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// setupFFIProject creates a temp dir with the given files and directories.
// Files are created from the map; dirs are created as empty directories.
func setupFFIProject(t *testing.T, files map[string]string, dirs []string) string {
	t.Helper()
	root := t.TempDir()
	for path, content := range files {
		full := filepath.Join(root, path)
		os.MkdirAll(filepath.Dir(full), 0755)
		if err := os.WriteFile(full, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	for _, d := range dirs {
		if err := os.MkdirAll(filepath.Join(root, d), 0755); err != nil {
			t.Fatal(err)
		}
	}
	return root
}

func TestResolveFFI_HappyPath(t *testing.T) {
	root := setupFFIProject(t,
		map[string]string{
			"src/ffi/bindings.c": "// bindings",
			"src/ffi/sensor.c":   "// sensor",
		},
		[]string{"src/ffi"},
	)

	m := &Manifest{
		FFI: FFIConfig{
			Sources:  []string{"src/ffi/bindings.c", "src/ffi/sensor.c"},
			Includes: []string{"src/ffi"},
			Defines:  map[string]string{"SENSOR_ADDR": "0x48"},
		},
	}

	resolved, err := ResolveFFI(m, root)
	if err != nil {
		t.Fatal(err)
	}
	if len(resolved.Sources) != 2 {
		t.Fatalf("expected 2 sources, got %d", len(resolved.Sources))
	}
	for _, s := range resolved.Sources {
		if !filepath.IsAbs(s) {
			t.Errorf("source path not absolute: %s", s)
		}
		if filepath.Ext(s) != ".c" {
			t.Errorf("source not .c: %s", s)
		}
	}
	if len(resolved.Includes) != 1 {
		t.Fatalf("expected 1 include, got %d", len(resolved.Includes))
	}
	if !filepath.IsAbs(resolved.Includes[0]) {
		t.Errorf("include path not absolute: %s", resolved.Includes[0])
	}
	if len(resolved.Defines) != 1 {
		t.Fatalf("expected 1 define, got %d", len(resolved.Defines))
	}
	if resolved.Defines[0] != "SENSOR_ADDR=0x48" {
		t.Errorf("unexpected define: %s", resolved.Defines[0])
	}
}

func TestResolveFFI_NoSources(t *testing.T) {
	m := &Manifest{}
	resolved, err := ResolveFFI(m, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if resolved != nil {
		t.Fatal("expected nil result when no FFI sources")
	}
}

func TestResolveFFI_IncludesWithoutSources(t *testing.T) {
	m := &Manifest{
		FFI: FFIConfig{
			Includes: []string{"src/ffi"},
		},
	}
	_, err := ResolveFFI(m, t.TempDir())
	if err == nil {
		t.Fatal("expected error for includes without sources")
	}
	if !strings.Contains(err.Error(), "without sources") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_DefinesWithoutSources(t *testing.T) {
	m := &Manifest{
		FFI: FFIConfig{
			Defines: map[string]string{"FOO": "1"},
		},
	}
	_, err := ResolveFFI(m, t.TempDir())
	if err == nil {
		t.Fatal("expected error for defines without sources")
	}
	if !strings.Contains(err.Error(), "without sources") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_MissingSource(t *testing.T) {
	root := setupFFIProject(t, nil, nil)
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{"src/ffi/missing.c"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for missing source")
	}
	if !strings.Contains(err.Error(), "missing.c") {
		t.Errorf("error should mention the missing file: %s", err)
	}
}

func TestResolveFFI_SourceNotCFile(t *testing.T) {
	root := setupFFIProject(t,
		map[string]string{"src/ffi/notes.txt": "not c"},
		nil,
	)
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{"src/ffi/notes.txt"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for non-.c source")
	}
	if !strings.Contains(err.Error(), "not a .c file") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_SourceIsDirectory(t *testing.T) {
	root := setupFFIProject(t, nil, []string{"src/ffi"})
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{"src/ffi"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error when source is a directory")
	}
	if !strings.Contains(err.Error(), "directory") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_SourceRootEscape(t *testing.T) {
	// Create a .c file outside the project root
	outside := t.TempDir()
	if err := os.WriteFile(filepath.Join(outside, "evil.c"), []byte("//"), 0644); err != nil {
		t.Fatal(err)
	}
	root := setupFFIProject(t, nil, nil)

	// Use relative path that escapes via ..
	rel, _ := filepath.Rel(root, filepath.Join(outside, "evil.c"))
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{rel},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for root escape")
	}
	if !strings.Contains(err.Error(), "escapes project root") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_IncludeRootEscape(t *testing.T) {
	outside := t.TempDir()
	root := setupFFIProject(t,
		map[string]string{"src/ffi/bindings.c": "//"},
		nil,
	)
	rel, _ := filepath.Rel(root, outside)
	m := &Manifest{
		FFI: FFIConfig{
			Sources:  []string{"src/ffi/bindings.c"},
			Includes: []string{rel},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for include root escape")
	}
	if !strings.Contains(err.Error(), "escapes project root") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_IncludeNotDirectory(t *testing.T) {
	root := setupFFIProject(t,
		map[string]string{
			"src/ffi/bindings.c": "//",
			"src/ffi/header.h":   "//",
		},
		nil,
	)
	m := &Manifest{
		FFI: FFIConfig{
			Sources:  []string{"src/ffi/bindings.c"},
			Includes: []string{"src/ffi/header.h"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error when include is a file")
	}
	if !strings.Contains(err.Error(), "not a directory") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_BadDefineKey(t *testing.T) {
	root := setupFFIProject(t,
		map[string]string{"src/ffi/bindings.c": "//"},
		nil,
	)
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{"src/ffi/bindings.c"},
			Defines: map[string]string{"123BAD": "val"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for bad define key")
	}
	if !strings.Contains(err.Error(), "not a valid C identifier") {
		t.Errorf("unexpected error: %s", err)
	}
}

func TestResolveFFI_BadDefineValue(t *testing.T) {
	root := setupFFIProject(t,
		map[string]string{"src/ffi/bindings.c": "//"},
		nil,
	)
	m := &Manifest{
		FFI: FFIConfig{
			Sources: []string{"src/ffi/bindings.c"},
			Defines: map[string]string{"GOOD_KEY": "has;semicolon"},
		},
	}
	_, err := ResolveFFI(m, root)
	if err == nil {
		t.Fatal("expected error for bad define value")
	}
	if !strings.Contains(err.Error(), "invalid characters") {
		t.Errorf("unexpected error: %s", err)
	}
}
