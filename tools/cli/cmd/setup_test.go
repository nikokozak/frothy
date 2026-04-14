package cmd

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRunSetupESPIDFUsesLocalScriptInsideRepo(t *testing.T) {
	resetCommandGlobals(t)

	repoRoot := t.TempDir()
	logPath := filepath.Join(t.TempDir(), "setup.log")
	mustWriteFile(t, filepath.Join(repoRoot, "CMakeLists.txt"), "project(Froth)\n")
	mustWriteFile(t, filepath.Join(repoRoot, "src", "froth_vm.h"), "/* vm */\n")
	mustWriteExecutable(t, filepath.Join(repoRoot, "tools", "setup-esp-idf.sh"), "#!/bin/sh\nprintf 'args=%s\\nFROTH_HOME=%s\\n' \"$*\" \"$FROTH_HOME\" > \""+logPath+"\"\n")

	if err := os.MkdirAll(filepath.Join(repoRoot, "nested"), 0755); err != nil {
		t.Fatalf("mkdir nested: %v", err)
	}
	withChdir(t, filepath.Join(repoRoot, "nested"))
	t.Setenv("FROTH_HOME", "/tmp/froth-home")

	if err := runSetup([]string{"esp-idf", "--force"}); err != nil {
		t.Fatalf("runSetup: %v", err)
	}

	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "args=--force") {
		t.Fatalf("setup log = %q, want --force", log)
	}
	if !strings.Contains(log, "FROTH_HOME=/tmp/froth-home") {
		t.Fatalf("setup log = %q, want FROTH_HOME", log)
	}
}

func TestRunSetupESPIDFDownloadsTaggedRawScriptOutsideRepo(t *testing.T) {
	resetCommandGlobals(t)

	logPath := filepath.Join(t.TempDir(), "setup.log")
	version := frothVersion(t)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		wantPath := fmt.Sprintf("/nikokozak/frothy/v%s/tools/setup-esp-idf.sh", version)
		if r.URL.Path != wantPath {
			http.NotFound(w, r)
			return
		}
		fmt.Fprintf(w, "#!/bin/sh\nprintf 'args=%%s\\nFROTH_HOME=%%s\\n' \"$*\" \"$FROTH_HOME\" > %q\n", logPath)
	}))
	defer server.Close()

	oldRawBase := rawContentBase
	rawContentBase = server.URL
	t.Cleanup(func() { rawContentBase = oldRawBase })

	withChdir(t, t.TempDir())
	t.Setenv("FROTH_HOME", "/tmp/froth-home")

	if err := runSetup([]string{"esp-idf", "--force"}); err != nil {
		t.Fatalf("runSetup: %v", err)
	}

	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "args=--force") {
		t.Fatalf("setup log = %q, want --force", log)
	}
	if !strings.Contains(log, "FROTH_HOME=/tmp/froth-home") {
		t.Fatalf("setup log = %q, want FROTH_HOME", log)
	}
}

func TestRunSetupESPIDFRejectsUnknownArgs(t *testing.T) {
	resetCommandGlobals(t)

	err := runSetup([]string{"esp-idf", "--definitely-not-real"})
	if err == nil {
		t.Fatal("runSetup succeeded, want error")
	}
	if !strings.Contains(err.Error(), "unknown argument") {
		t.Fatalf("error = %v, want unknown argument", err)
	}
}

func TestRawTaggedURLUsesSingleRepoPrefix(t *testing.T) {
	url := rawTaggedURL("1.2.3", "tools/setup-esp-idf.sh")
	want := "https://raw.githubusercontent.com/nikokozak/frothy/v1.2.3/tools/setup-esp-idf.sh"
	if url != want {
		t.Fatalf("rawTaggedURL = %q, want %q", url, want)
	}
}

func TestRawTaggedURLUsesReleaseRepoSlugOverride(t *testing.T) {
	t.Setenv("RELEASE_REPO_SLUG", "example/frothy-nightly")

	url := rawTaggedURL("1.2.3", "tools/setup-esp-idf.sh")
	want := "https://raw.githubusercontent.com/example/frothy-nightly/v1.2.3/tools/setup-esp-idf.sh"
	if url != want {
		t.Fatalf("rawTaggedURL = %q, want %q", url, want)
	}
}
