package sdk

import (
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

func TestEnsureSDKFromFSConcurrentCallsAreAtomic(t *testing.T) {
	fsys := testPayloadFS(t, map[string]string{
		"CMakeLists.txt":                      "cmake_minimum_required(VERSION 3.23)\nset(FROTH_VERSION \"0.1.0\" CACHE STRING \"Froth version string\")\n",
		"src/froth_vm.h":                      "/* vm */\n",
		"src/lib/core.froth":                  ": dup dup ;\n",
		"boards/posix/ffi.c":                  "/* board */\n",
		"boards/posix/ffi.h":                  "/* board header */\n",
		"boards/posix/board.json":             "{\"board\":\"posix\"}\n",
		"platforms/posix/platform.c":          "/* platform */\n",
		"cmake/embed_froth.cmake":             "# embed\n",
		"targets/esp-idf/CMakeLists.txt":      "# target\n",
		"targets/esp-idf/main/main.c":         "/* main */\n",
		"targets/esp-idf/main/CMakeLists.txt": "# main cmake\n",
	})
	version, err := VersionFromFS(fsys)
	if err != nil {
		t.Fatalf("VersionFromFS: %v", err)
	}

	frothHome := t.TempDir()
	const callers = 8

	paths := make([]string, callers)
	errs := make([]error, callers)

	var wg sync.WaitGroup
	wg.Add(callers)
	for i := 0; i < callers; i++ {
		go func(i int) {
			defer wg.Done()
			paths[i], errs[i] = ensureSDKFromFS(fsys, frothHome, version)
		}(i)
	}
	wg.Wait()

	for i, err := range errs {
		if err != nil {
			t.Fatalf("ensureSDKFromFS call %d: %v", i, err)
		}
	}

	wantRoot := filepath.Join(frothHome, "sdk", "frothy-"+version)
	for i, path := range paths {
		if path != wantRoot {
			t.Fatalf("paths[%d] = %q, want %q", i, path, wantRoot)
		}
	}

	entries, err := os.ReadDir(filepath.Join(frothHome, "sdk"))
	if err != nil {
		t.Fatalf("ReadDir sdk cache: %v", err)
	}
	if len(entries) != 1 || entries[0].Name() != "frothy-"+version {
		t.Fatalf("sdk cache entries = %v, want only frothy-%s", entryNames(entries), version)
	}
}

func TestVersionFromFSReadsEmbeddedVersion(t *testing.T) {
	fsys := testPayloadFS(t, map[string]string{
		"CMakeLists.txt":                      "cmake_minimum_required(VERSION 3.23)\nset(FROTH_VERSION \"0.1.0\" CACHE STRING \"Froth version string\")\n",
		"src/froth_vm.h":                      "/* vm */\n",
		"src/lib/core.froth":                  ": dup dup ;\n",
		"boards/posix/ffi.c":                  "/* board */\n",
		"boards/posix/ffi.h":                  "/* board header */\n",
		"boards/posix/board.json":             "{\"board\":\"posix\"}\n",
		"platforms/posix/platform.c":          "/* platform */\n",
		"cmake/embed_froth.cmake":             "# embed\n",
		"targets/esp-idf/CMakeLists.txt":      "# target\n",
		"targets/esp-idf/main/main.c":         "/* main */\n",
		"targets/esp-idf/main/CMakeLists.txt": "# main cmake\n",
	})
	version, err := VersionFromFS(fsys)
	if err != nil {
		t.Fatalf("VersionFromFS: %v", err)
	}
	want, err := repoVersion(t)
	if err != nil {
		t.Fatalf("repoVersion: %v", err)
	}
	if version != want {
		t.Fatalf("version = %q, want %q", version, want)
	}
}

func repoVersion(t *testing.T) (string, error) {
	t.Helper()

	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}

	dir := wd
	for {
		path := filepath.Join(dir, "VERSION")
		data, err := os.ReadFile(path)
		if err == nil {
			return strings.TrimSpace(string(data)), nil
		}
		if !os.IsNotExist(err) {
			return "", err
		}

		parent := filepath.Dir(dir)
		if parent == dir {
			return "", os.ErrNotExist
		}
		dir = parent
	}
}

func entryNames(entries []os.DirEntry) []string {
	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		names = append(names, entry.Name())
	}
	return names
}
