package project

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func setupProject(t *testing.T, files map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	for path, content := range files {
		full := filepath.Join(dir, path)
		os.MkdirAll(filepath.Dir(full), 0755)
		if err := os.WriteFile(full, []byte(content), 0644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func mustResolve(t *testing.T, root string) *ResolveResult {
	t.Helper()
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}
	result, err := Resolve(m, root)
	if err != nil {
		t.Fatal(err)
	}
	return result
}

func mustFailResolve(t *testing.T, root string, wantSubstr string) {
	t.Helper()
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}
	_, err = Resolve(m, root)
	if err == nil {
		t.Fatalf("expected error containing %q, got nil", wantSubstr)
	}
	if !strings.Contains(err.Error(), wantSubstr) {
		t.Fatalf("expected error containing %q, got: %s", wantSubstr, err)
	}
}

// ============================================================
// Basic resolution
// ============================================================

func TestResolveSimple(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": `: main "hello" s.emit ;`,
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, ": main") {
		t.Error("missing main definition")
	}
	if !strings.Contains(result.Source, "--- src/main.froth ---") {
		t.Error("missing boundary marker")
	}
	runtimeSource := StripBoundaryMarkers(result.Source)
	if strings.Contains(runtimeSource, "--- src/main.froth ---") {
		t.Error("StripBoundaryMarkers should remove file boundary markers")
	}
}

func TestResolveWithDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
helper = { path = "lib/helper.froth" }`,
		"lib/helper.froth": `: double dup + ;`,
		"src/main.froth":   "\\ #use \"helper\"\n: main 5 double . ;",
	})
	result := mustResolve(t, root)

	helperIdx := strings.Index(result.Source, "--- lib/helper.froth ---")
	mainIdx := strings.Index(result.Source, "--- src/main.froth ---")
	if helperIdx < 0 || mainIdx < 0 {
		t.Fatal("missing boundary markers")
	}
	if helperIdx >= mainIdx {
		t.Error("dependency should appear before entry")
	}
}

func TestResolveRelativePath(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":        `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/helpers.froth": `: helper 99 ;`,
		"src/main.froth":    "\\ #use \"./helpers.froth\"\n: main helper . ;",
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, ": helper 99 ;") {
		t.Error("missing helper definition")
	}
}

func TestResolveDirectoryDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
stepper = { path = "lib/stepper/" }`,
		"lib/stepper/init.froth": `: step.init 1 ;`,
		"src/main.froth":         "\\ #use \"stepper\"\n: main step.init ;",
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, ": step.init 1 ;") {
		t.Error("missing stepper init definition")
	}
}

// ============================================================
// Diamond dependencies (dedup)
// ============================================================

func TestResolveDiamond(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }
common = { path = "lib/common.froth" }`,
		"lib/common.froth": `: shared 42 ;`,
		"lib/a.froth":      "\\ #use \"common\"\n: a-word shared . ;",
		"lib/b.froth":      "\\ #use \"common\"\n: b-word shared . ;",
		"src/main.froth":   "\\ #use \"a\"\n\\ #use \"b\"\n: main a-word b-word ;",
	})
	result := mustResolve(t, root)

	count := strings.Count(result.Source, "--- lib/common.froth ---")
	if count != 1 {
		t.Errorf("common should appear once, got %d", count)
	}

	commonIdx := strings.Index(result.Source, "--- lib/common.froth ---")
	aIdx := strings.Index(result.Source, "--- lib/a.froth ---")
	bIdx := strings.Index(result.Source, "--- lib/b.froth ---")
	mainIdx := strings.Index(result.Source, "--- src/main.froth ---")
	if !(commonIdx < aIdx && aIdx < bIdx && bIdx < mainIdx) {
		t.Error("expected order: common < a < b < main")
	}
}

func TestResolveSameFileNamedAndRelative(t *testing.T) {
	// Same file reached via named dep and relative path — should be included once
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
helper = { path = "lib/helper.froth" }`,
		"lib/helper.froth": `: helper 1 ;`,
		"lib/user.froth":   "\\ #use \"../lib/helper.froth\"\n: user helper ;",
		"src/main.froth":   "\\ #use \"helper\"\n\\ #use \"../lib/user.froth\"\n: main user ;",
	})
	result := mustResolve(t, root)

	count := strings.Count(result.Source, "--- lib/helper.froth ---")
	if count != 1 {
		t.Errorf("helper should appear once (dedup across named + relative), got %d", count)
	}
}

// ============================================================
// Cycle detection
// ============================================================

func TestResolveCycle(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }`,
		"lib/a.froth":    "\\ #use \"b\"\n: a-word 1 ;",
		"lib/b.froth":    "\\ #use \"a\"\n: b-word 2 ;",
		"src/main.froth": "\\ #use \"a\"\n: main a-word ;",
	})
	mustFailResolve(t, root, "circular")
}

func TestResolveSelfInclude(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\\ #use \"./main.froth\"\n: main 1 ;",
	})
	mustFailResolve(t, root, "includes itself")
}

func TestResolveThreeNodeCycle(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }
c = { path = "lib/c.froth" }`,
		"lib/a.froth":    "\\ #use \"b\"\n: a 1 ;",
		"lib/b.froth":    "\\ #use \"c\"\n: b 2 ;",
		"lib/c.froth":    "\\ #use \"a\"\n: c 3 ;",
		"src/main.froth": "\\ #use \"a\"\n: main a ;",
	})
	mustFailResolve(t, root, "circular")
}

// ============================================================
// Root escape
// ============================================================

func TestResolveRootEscape(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\\ #use \"../../etc/passwd\"\n: main 1 ;",
	})
	// Path escapes root — rejected either as "escape" or "not found"
	// (depends on whether os.Stat or root check fires first)
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}
	_, err = Resolve(m, root)
	if err == nil {
		t.Fatal("expected error for root escape")
	}
}

// ============================================================
// Context-aware scanner
// ============================================================

func TestResolveDirectiveInsideParenComment(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "( \\ #use \"nonexistent\" )\n: main 1 ;",
	})
	result := mustResolve(t, root)
	if !strings.Contains(result.Source, ": main 1 ;") {
		t.Error("directive inside paren comment should be ignored")
	}
}

func TestResolveDirectiveInsideLineComment(t *testing.T) {
	// A line comment before the \ #use — the whole line is a comment
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\\ this is a comment \\ #use \"nonexistent\"\n: main 1 ;",
	})
	result := mustResolve(t, root)
	if !strings.Contains(result.Source, ": main 1 ;") {
		t.Error("directive after line comment should be ignored")
	}
}

func TestResolveDirectiveInsideMultiLineComment(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "( this is a\n\\ #use \"nonexistent\"\nmulti-line comment )\n: main 1 ;",
	})
	result := mustResolve(t, root)
	if !strings.Contains(result.Source, ": main 1 ;") {
		t.Error("directive inside multi-line paren comment should be ignored")
	}
}

func TestResolveDirectiveInsideString(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
real = { path = "lib/real.froth" }`,
		"lib/real.froth": `: real 1 ;`,
		"src/main.froth": ": main \"text \\\\ #use \\\"real\\\"\" s.emit ;",
	})
	// The \ #use "real" is inside a string literal.
	// It should NOT be treated as a directive. "real" should NOT be included.
	result := mustResolve(t, root)
	if strings.Contains(result.Source, "--- lib/real.froth ---") {
		t.Error("directive inside string should not trigger include")
	}
	if !strings.Contains(result.Source, ": main") {
		t.Error("main definition should be present")
	}
}

func TestResolveDirectiveMidLine(t *testing.T) {
	// `\ #use` after code on the same line should NOT trigger as a directive
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
dep = { path = "lib/dep.froth" }`,
		"lib/dep.froth":  `: dep 1 ;`,
		"src/main.froth": "1 2 + \\ #use \"dep\"\n: main 3 ;",
	})
	result := mustResolve(t, root)
	// The directive is mid-line — should be ignored, dep NOT included
	if strings.Contains(result.Source, "--- lib/dep.froth ---") {
		t.Error("mid-line \\ #use should not trigger an include")
	}
}

func TestResolveDirectiveIndented(t *testing.T) {
	// Indented `\ #use` should still work (whitespace before `\`)
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
dep = { path = "lib/dep.froth" }`,
		"lib/dep.froth":  `: dep 1 ;`,
		"src/main.froth": "    \\ #use \"dep\"\n: main dep ;",
	})
	result := mustResolve(t, root)
	if !strings.Contains(result.Source, "--- lib/dep.froth ---") {
		t.Error("indented \\ #use should work")
	}
}

// ============================================================
// Library discipline
// ============================================================

func TestResolveLibraryDisciplineWarning(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }`,
		"lib/bad.froth":  ": helper 1 ;\n\"side effect\" s.emit",
		"src/main.froth": "\\ #use \"bad\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Error("expected warning for top-level string form in library")
	}
}

func TestResolveLibraryDisciplineBareWordCall(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }`,
		"lib/bad.froth":  ": helper 1 ;\nledc.init",
		"src/main.froth": "\\ #use \"bad\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Error("expected warning for bare word call in library")
	}
}

func TestResolveLibraryTickCallWarning(t *testing.T) {
	// `'name call` is NOT safe — it executes
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }`,
		"lib/bad.froth":  ": helper 1 ;\n'helper call",
		"src/main.froth": "\\ #use \"bad\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Error("expected warning for 'name call in library")
	}
}

func TestResolveLibraryTickDefSafe(t *testing.T) {
	// `'name value def` IS safe
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
good = { path = "lib/good.froth" }`,
		"lib/good.froth": "'counter 0 def\n: helper counter ;",
		"src/main.froth": "\\ #use \"good\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("tick-def should not warn, got: %v", result.Warnings)
	}
}

func TestResolveAllowToplevel(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
init = { path = "lib/init.froth" }`,
		"lib/init.froth": "\\ #allow-toplevel\nledc.init\n: helper 1 ;",
		"src/main.froth": "\\ #use \"init\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("expected no warnings with #allow-toplevel, got: %v", result.Warnings)
	}
}

func TestResolveSemicolonInStackComment(t *testing.T) {
	// `;` inside ( ... ) comment should NOT end a definition
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
lib = { path = "lib/lib.froth" }`,
		"lib/lib.froth":  ": helper ( n -- n ; identity )\n  dup drop\n;",
		"src/main.froth": "\\ #use \"lib\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("semicolon in stack comment should not cause warning, got: %v", result.Warnings)
	}
}

func TestResolveMultiLineParenCommentInLibrary(t *testing.T) {
	// Multi-line paren comment should not trigger warnings
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
lib = { path = "lib/lib.froth" }`,
		"lib/lib.froth":  "( This function\n  computes things )\n: helper 1 ;",
		"src/main.froth": "\\ #use \"lib\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("multi-line comment should not cause warning, got: %v", result.Warnings)
	}
}

func TestResolveEntryFileNoWarnings(t *testing.T) {
	// Entry file can have any top-level forms — no warnings
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\"hello\" s.emit cr\n1 2 + .\n: autorun 42 ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("entry file should never have warnings, got: %v", result.Warnings)
	}
}

// ============================================================
// Line number accuracy
// ============================================================

func TestResolveWarningLineNumbers(t *testing.T) {
	// After stripping #use directives, line numbers should still match the original file
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }
other = { path = "lib/other.froth" }`,
		"lib/other.froth": `: other 1 ;`,
		"lib/bad.froth":   "\\ #use \"other\"\n\\ this is a comment\n: helper 1 ;\nbare-word-call",
		"src/main.froth":  "\\ #use \"bad\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Fatal("expected warning for bare-word-call")
	}
	// bare-word-call is on line 4 of the original file
	if !strings.Contains(result.Warnings[0], ":4:") {
		t.Errorf("expected line 4, got: %s", result.Warnings[0])
	}
}

// ============================================================
// Empty and whitespace files
// ============================================================

func TestResolveEmptyFile(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "",
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, "--- src/main.froth ---") {
		t.Error("empty file should still produce boundary marker")
	}
}

func TestResolveWhitespaceOnlyFile(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "   \n\n  \n",
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, "--- src/main.froth ---") {
		t.Error("whitespace-only file should still produce boundary marker")
	}
}

func TestResolveEmptyDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
empty = { path = "lib/empty.froth" }`,
		"lib/empty.froth": "",
		"src/main.froth":  "\\ #use \"empty\"\n: main 1 ;",
	})
	result := mustResolve(t, root)

	if !strings.Contains(result.Source, ": main 1 ;") {
		t.Error("empty dependency should not break resolution")
	}
}

func TestResolveCommentOnlyDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
comments = { path = "lib/comments.froth" }`,
		"lib/comments.froth": "\\ This file is just comments\n\\ Nothing here\n( and a paren comment )",
		"src/main.froth":     "\\ #use \"comments\"\n: main 1 ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("comment-only file should not warn, got: %v", result.Warnings)
	}
}

// ============================================================
// Manifest edge cases
// ============================================================

func TestManifestDefaults(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]` + "\n" + `name = "test"`,
	})
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}

	if m.Project.Entry != "src/main.froth" {
		t.Errorf("default entry: got %q, want src/main.froth", m.Project.Entry)
	}
	if m.Target.Board != "posix" {
		t.Errorf("default board: got %q, want posix", m.Target.Board)
	}
	if m.Target.Platform != "posix" {
		t.Errorf("default platform: got %q, want posix", m.Target.Platform)
	}
}

func TestManifestMissingName(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]` + "\n" + `version = "0.1.0"`,
	})

	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}
	if m.Project.Name != "" {
		t.Error("expected empty name from manifest")
	}

	_, _, err = Load(root)
	if err == nil || !strings.Contains(err.Error(), "name") {
		t.Errorf("expected name-required error, got: %v", err)
	}
}

func TestManifestEmptyDependencyPath(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "" }`,
		"src/main.froth": "\\ #use \"bad\"\n: main 1 ;",
	})
	m, err := LoadManifest(filepath.Join(root, "froth.toml"))
	if err != nil {
		t.Fatal(err)
	}
	_, err = Resolve(m, root)
	if err == nil {
		t.Fatal("expected error for empty dependency path")
	}
	if !strings.Contains(err.Error(), "empty path") {
		t.Errorf("expected empty path error, got: %s", err)
	}
}

func TestManifestCMakeArgs(t *testing.T) {
	heap := 8192
	cell := 64
	b := BuildConfig{
		HeapSize: &heap,
		CellSize: &cell,
	}
	args := b.CMakeArgs()
	if len(args) != 2 {
		t.Errorf("expected 2 args, got %d: %v", len(args), args)
	}
	found := false
	for _, a := range args {
		if a == "-DFROTH_HEAP_SIZE=8192" {
			found = true
		}
	}
	if !found {
		t.Errorf("expected -DFROTH_HEAP_SIZE=8192 in args: %v", args)
	}
}

func TestManifestCMakeArgsEmpty(t *testing.T) {
	b := BuildConfig{}
	args := b.CMakeArgs()
	if len(args) != 0 {
		t.Errorf("expected 0 args for empty build config, got %d", len(args))
	}
}

// ============================================================
// Dependency not found
// ============================================================

func TestResolveMissingDependency(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
missing = { path = "lib/missing.froth" }`,
		"src/main.froth": "\\ #use \"missing\"\n: main 1 ;",
	})
	mustFailResolve(t, root, "not found")
}

func TestResolveUndeclaredNamedDep(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\\ #use \"undeclared\"\n: main 1 ;",
	})
	mustFailResolve(t, root, "undeclared")
}

func TestResolveMissingRelative(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml":     `[project]` + "\n" + `name = "test"` + "\n" + `entry = "src/main.froth"`,
		"src/main.froth": "\\ #use \"./nonexistent.froth\"\n: main 1 ;",
	})
	mustFailResolve(t, root, "not found")
}

// ============================================================
// Deep transitive dependencies
// ============================================================

func TestResolveDeepChain(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }
c = { path = "lib/c.froth" }
d = { path = "lib/d.froth" }`,
		"lib/d.froth":    `: d 4 ;`,
		"lib/c.froth":    "\\ #use \"d\"\n: c d ;",
		"lib/b.froth":    "\\ #use \"c\"\n: b c ;",
		"lib/a.froth":    "\\ #use \"b\"\n: a b ;",
		"src/main.froth": "\\ #use \"a\"\n: main a ;",
	})
	result := mustResolve(t, root)

	// Should be ordered d < c < b < a < main
	dIdx := strings.Index(result.Source, "--- lib/d.froth ---")
	cIdx := strings.Index(result.Source, "--- lib/c.froth ---")
	bIdx := strings.Index(result.Source, "--- lib/b.froth ---")
	aIdx := strings.Index(result.Source, "--- lib/a.froth ---")
	mainIdx := strings.Index(result.Source, "--- src/main.froth ---")
	if !(dIdx < cIdx && cIdx < bIdx && bIdx < aIdx && aIdx < mainIdx) {
		t.Error("expected order: d < c < b < a < main")
	}
}

// ============================================================
// Duplicate definitions
// ============================================================

func TestResolveDuplicateDefinitionAcrossLibraries(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
a = { path = "lib/a.froth" }
b = { path = "lib/b.froth" }`,
		"lib/a.froth":    ": helper 1 ;",
		"lib/b.froth":    ": helper 2 ;",
		"src/main.froth": "\\ #use \"a\"\n\\ #use \"b\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Error("expected duplicate definition warning")
	}
	foundDup := false
	for _, w := range result.Warnings {
		if strings.Contains(w, "duplicate") && strings.Contains(w, "helper") {
			foundDup = true
		}
	}
	if !foundDup {
		t.Errorf("expected duplicate warning for 'helper', got: %v", result.Warnings)
	}
}

func TestResolveDuplicateEntryOverrideAllowed(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
lib = { path = "lib/lib.froth" }`,
		"lib/lib.froth":  ": helper 1 ;",
		"src/main.froth": "\\ #use \"lib\"\n: helper 99 ;\n: main helper ;",
	})
	result := mustResolve(t, root)

	// Entry file redefining a library word is allowed — no duplicate warning
	for _, w := range result.Warnings {
		if strings.Contains(w, "duplicate") {
			t.Errorf("entry file should be allowed to override, got: %s", w)
		}
	}
}

// ============================================================
// Trailing content after semicolon
// ============================================================

func TestResolveTrailingContentAfterSemicolon(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
bad = { path = "lib/bad.froth" }`,
		"lib/bad.froth":  ": helper 1 ; ledc.init",
		"src/main.froth": "\\ #use \"bad\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) == 0 {
		t.Error("expected warning for content after semicolon")
	}
}

func TestResolveTrailingCommentAfterSemicolonOK(t *testing.T) {
	root := setupProject(t, map[string]string{
		"froth.toml": `[project]
name = "test"
entry = "src/main.froth"
[dependencies]
good = { path = "lib/good.froth" }`,
		"lib/good.froth": ": helper 1 ; \\ this is a comment",
		"src/main.froth": "\\ #use \"good\"\n: main helper ;",
	})
	result := mustResolve(t, root)

	if len(result.Warnings) != 0 {
		t.Errorf("trailing comment after ; should not warn, got: %v", result.Warnings)
	}
}
