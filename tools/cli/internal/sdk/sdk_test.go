package sdk

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"testing"
	"testing/fstest"
)

func TestFrothHomeUsesEnvironmentOverride(t *testing.T) {
	t.Setenv("FROTH_HOME", "/tmp/froth-home")

	home, err := FrothHome()
	if err != nil {
		t.Fatalf("FrothHome: %v", err)
	}
	if home != "/tmp/froth-home" {
		t.Fatalf("home = %q, want %q", home, "/tmp/froth-home")
	}
}

func TestEnsureSDKExtractsEmbeddedTree(t *testing.T) {
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
	sdkRoot, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("ensureSDKFromFS: %v", err)
	}

	wantRoot := filepath.Join(frothHome, "sdk", "froth-"+version)
	if sdkRoot != wantRoot {
		t.Fatalf("sdk root = %q, want %q", sdkRoot, wantRoot)
	}

	assertFileContents(t, sdkRoot, "CMakeLists.txt", `cmake_minimum_required(VERSION 3.23)
set(FROTH_VERSION "0.1.0" CACHE STRING "Froth version string")
`)
	assertFileContents(t, sdkRoot, filepath.Join("src", "froth_vm.h"), "/* vm */\n")
	assertFileContents(t, sdkRoot, filepath.Join("src", "lib", "core.froth"), ": dup dup ;\n")
	assertFileContents(t, sdkRoot, filepath.Join("boards", "posix", "ffi.c"), "/* board */\n")
	assertFileContents(t, sdkRoot, filepath.Join("boards", "posix", "board.json"), "{\"board\":\"posix\"}\n")
	assertFileContents(t, sdkRoot, filepath.Join("platforms", "posix", "platform.c"), "/* platform */\n")
	assertFileContents(t, sdkRoot, filepath.Join("cmake", "embed_froth.cmake"), "# embed\n")
	assertFileContents(t, sdkRoot, "VERSION", version+"\n")
}

func TestEnsureSDKSkipsExistingVersion(t *testing.T) {
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
	sdkRoot, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("first ensureSDKFromFS: %v", err)
	}

	markerPath := filepath.Join(sdkRoot, "marker.txt")
	if err := os.WriteFile(markerPath, []byte("keep"), 0644); err != nil {
		t.Fatalf("write marker: %v", err)
	}

	sdkRoot2, err := ensureSDKFromFS(fsys, frothHome, version)
	if err != nil {
		t.Fatalf("second ensureSDKFromFS: %v", err)
	}
	if sdkRoot2 != sdkRoot {
		t.Fatalf("second sdk root = %q, want %q", sdkRoot2, sdkRoot)
	}
	assertFileContents(t, sdkRoot, "marker.txt", "keep")
}

func TestEnsureSDKRefreshesStaleExistingVersion(t *testing.T) {
	oldFS := testPayloadFS(t, map[string]string{
		"CMakeLists.txt":             "cmake_minimum_required(VERSION 3.23)\nset(FROTH_VERSION \"0.1.0\" CACHE STRING \"Froth version string\")\n",
		"src/froth_vm.h":             "/* old vm */\n",
		"boards/posix/ffi.c":         "/* old board */\n",
		"platforms/posix/platform.c": "/* old platform */\n",
	})
	version, err := VersionFromFS(oldFS)
	if err != nil {
		t.Fatalf("VersionFromFS(oldFS): %v", err)
	}

	frothHome := t.TempDir()
	sdkRoot, err := ensureSDKFromFS(oldFS, frothHome, version)
	if err != nil {
		t.Fatalf("ensureSDKFromFS(oldFS): %v", err)
	}
	assertFileContents(t, sdkRoot, filepath.Join("src", "froth_vm.h"), "/* old vm */\n")

	newFS := testPayloadFS(t, map[string]string{
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
	sdkRoot, err = ensureSDKFromFS(newFS, frothHome, version)
	if err != nil {
		t.Fatalf("ensureSDKFromFS(newFS): %v", err)
	}
	assertFileContents(t, sdkRoot, filepath.Join("src", "froth_vm.h"), "/* vm */\n")
	assertFileContents(t, sdkRoot, filepath.Join("boards", "posix", "board.json"), "{\"board\":\"posix\"}\n")
}

func testPayloadFS(t *testing.T, files map[string]string) fs.FS {
	t.Helper()

	version, err := repoVersion(t)
	if err != nil {
		t.Fatalf("repoVersion: %v", err)
	}
	if _, ok := files["VERSION"]; !ok {
		files["VERSION"] = version + "\n"
	}

	archive, err := buildTestArchive(files)
	if err != nil {
		t.Fatalf("buildTestArchive: %v", err)
	}

	digest := sha256.Sum256(archive)
	return fstest.MapFS{
		"VERSION":        {Data: []byte(version + "\n")},
		"PAYLOAD_SHA256": {Data: []byte(hex.EncodeToString(digest[:]) + "\n")},
		"kernel.tar.gz":  {Data: archive},
	}
}

func assertFileContents(t *testing.T, root string, relPath string, want string) {
	t.Helper()

	got, err := os.ReadFile(filepath.Join(root, relPath))
	if err != nil {
		t.Fatalf("read %s: %v", relPath, err)
	}
	if string(got) != want {
		t.Fatalf("%s = %q, want %q", relPath, string(got), want)
	}
}

func buildTestArchive(files map[string]string) ([]byte, error) {
	var paths []string
	for path := range files {
		paths = append(paths, path)
	}
	sort.Strings(paths)

	var buf bytes.Buffer
	gzw := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gzw)
	for _, name := range paths {
		data := []byte(files[name])
		header := &tar.Header{
			Name: name,
			Mode: 0644,
			Size: int64(len(data)),
		}
		if err := tw.WriteHeader(header); err != nil {
			_ = tw.Close()
			_ = gzw.Close()
			return nil, err
		}
		if _, err := tw.Write(data); err != nil {
			_ = tw.Close()
			_ = gzw.Close()
			return nil, err
		}
	}
	if err := tw.Close(); err != nil {
		_ = gzw.Close()
		return nil, err
	}
	if err := gzw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
