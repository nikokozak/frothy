package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/frothy/tools/cli/internal/project"
)

func stubSeedBuiltImage(t *testing.T) {
	t.Helper()

	oldSeed := seedBuiltImage
	seedBuiltImage = func(string, string, string) error { return nil }
	t.Cleanup(func() { seedBuiltImage = oldSeed })
}

func TestRunBuildManifestWritesResolvedSourceWithoutAutorun(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
dep = { path = "lib/dep.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "lib", "dep.froth"), ": dep-word 41 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "helper.froth"), ": helper 1 ;")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "dep"
\ #use "./helper.froth"

: autorun
  dep-word helper +
;
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	resolved := mustReadFile(t, filepath.Join(projectRoot, ".froth-build", "resolved.froth"))
	depMarker := `\ --- lib/dep.froth ---`
	helperMarker := `\ --- src/helper.froth ---`
	mainMarker := `\ --- src/main.froth ---`
	depIndex := strings.Index(resolved, depMarker)
	helperIndex := strings.Index(resolved, helperMarker)
	mainIndex := strings.Index(resolved, mainMarker)
	if depIndex < 0 || helperIndex < 0 || mainIndex < 0 {
		t.Fatalf("resolved source missing expected markers:\n%s", resolved)
	}
	if !(depIndex < helperIndex && helperIndex < mainIndex) {
		t.Fatalf("marker order wrong:\n%s", resolved)
	}
	if strings.Contains(resolved, "[ 'autorun call ] catch drop drop") {
		t.Fatalf("resolved source unexpectedly contains autorun invocation:\n%s", resolved)
	}
}

func TestRunBuildManifestSeedsSnapshotFromProjectRoot(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	var seededBinary, seededRunDir, seededSource string
	oldSeed := seedBuiltImage
	seedBuiltImage = func(binaryPath string, runDir string, runtimeSourcePath string) error {
		seededBinary = binaryPath
		seededRunDir = runDir
		seededSource = runtimeSourcePath
		return nil
	}
	t.Cleanup(func() { seedBuiltImage = oldSeed })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	if seededBinary != filepath.Join(projectRoot, ".froth-build", "firmware", "Frothy") {
		t.Fatalf("seeded binary = %q", seededBinary)
	}
	if seededRunDir != projectRoot {
		t.Fatalf("seeded run dir = %q, want %q", seededRunDir, projectRoot)
	}
	if seededSource != filepath.Join(projectRoot, ".froth-build", "runtime.frothy") {
		t.Fatalf("seeded source = %q", seededSource)
	}
}

func TestRunBuildRejectsLegacyTargetFlagInsideManifestProject(t *testing.T) {
	resetCommandGlobals(t)
	targetFlag = "esp-idf"

	projectRoot := t.TempDir()
	withChdir(t, projectRoot)
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	err := runBuild()
	if err == nil {
		t.Fatal("runBuild succeeded, want error")
	}
	if !strings.Contains(err.Error(), "does not accept --target inside a project") {
		t.Fatalf("runBuild error = %v, want manifest target guidance", err)
	}
}

func TestRunBuildManifestPassesBuildOverridesToCMake(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[build]
cell_size = 64
heap_size = 8192
slot_table_size = 256
line_buffer_size = 2048
tbuf_size = 4096
tdesc_max = 16
ffi_max_tables = 12
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), ": autorun ;")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	log := mustReadFile(t, logPath)
	for _, want := range []string{
		"-DFROTH_CELL_SIZE_BITS=64",
		"-DFROTH_HEAP_SIZE=8192",
		"-DFROTH_SLOT_TABLE_SIZE=256",
		"-DFROTH_LINE_BUFFER_SIZE=2048",
		"-DFROTH_TBUF_SIZE=4096",
		"-DFROTH_TDESC_MAX=16",
		"-DFROTH_FFI_MAX_TABLES=12",
	} {
		if !strings.Contains(log, want) {
			t.Fatalf("build log = %q, want %q", log, want)
		}
	}
}

func TestRunBuildManifestFallsBackToGMake(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "cmake"), "#!/bin/sh\nprintf 'cmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(toolsDir, "gmake"), "#!/bin/sh\nprintf 'gmake %s\\n' \"$*\" >> \""+logPath+"\"\nexit 0\n")
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	oldBuildLookPath := buildLookPath
	buildLookPath = func(name string) (string, error) {
		if name == "make" {
			return "", exec.ErrNotFound
		}
		return exec.LookPath(name)
	}
	t.Cleanup(func() { buildLookPath = oldBuildLookPath })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), ": autorun ;")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "gmake") {
		t.Fatalf("build log = %q, want gmake invocation", log)
	}
}

func TestRunBuildManifestCleanDeletesExistingBuildDir(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)
	cleanFlag = true

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), ": autorun ;")
	mustWriteFile(t, filepath.Join(projectRoot, ".froth-build", "stale.txt"), "stale")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	if _, err := os.Stat(filepath.Join(projectRoot, ".froth-build", "stale.txt")); !os.IsNotExist(err) {
		t.Fatalf("stale file still exists after clean build: %v", err)
	}
}

func TestRunBuildManifestMissingEntryFileErrorsWithPath(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/missing.froth"

[target]
board = "posix"
platform = "posix"
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	err = runBuildManifest(manifest, root)
	if err == nil {
		t.Fatal("runBuildManifest succeeded, want error")
	}

	wantPath := filepath.Join(projectRoot, "src", "missing.froth")
	if !strings.Contains(err.Error(), wantPath) {
		t.Fatalf("error = %v, want path %s", err, wantPath)
	}
}

func TestRunBuildManifestMissingDependencyErrorsWithFileContext(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
missing = { path = "lib/missing.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), `\ #use "missing"
: autorun ;
`)

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	err = runBuildManifest(manifest, root)
	if err == nil {
		t.Fatal("runBuildManifest succeeded, want error")
	}
	if !strings.Contains(err.Error(), "src/main.froth:1") {
		t.Fatalf("error = %v, want source location", err)
	}
	if !strings.Contains(err.Error(), "lib/missing.froth") {
		t.Fatalf("error = %v, want missing dependency path", err)
	}
}

func TestRunBuildManifestProjectFFIGeneratesConfig(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "ffi-demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[ffi]
sources = ["src/ffi/bindings.c"]
includes = ["src/ffi"]
defines = { SENSOR_ADDR = "0x48" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")
	mustWriteFile(t, filepath.Join(projectRoot, "src", "ffi", "bindings.c"), "// bindings")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	// Config file should exist with absolute paths
	configPath := filepath.Join(projectRoot, ".froth-build", "project_ffi.cmake")
	config := mustReadFile(t, configPath)

	if !strings.Contains(config, "FROTH_PROJECT_FFI_SOURCES") {
		t.Fatal("config missing FROTH_PROJECT_FFI_SOURCES")
	}
	if !strings.Contains(config, filepath.Join(projectRoot, "src", "ffi", "bindings.c")) {
		t.Fatalf("config missing absolute source path:\n%s", config)
	}
	if !strings.Contains(config, filepath.Join(projectRoot, "src", "ffi")) {
		t.Fatalf("config missing absolute include path:\n%s", config)
	}
	if !strings.Contains(config, "SENSOR_ADDR=0x48") {
		t.Fatalf("config missing define:\n%s", config)
	}

	// Build log should contain -DFROTH_PROJECT_FFI_CONFIG=...
	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "-DFROTH_PROJECT_FFI_CONFIG=") {
		t.Fatalf("build log missing FFI config arg:\n%s", log)
	}
}

func TestRunBuildManifestProjectFFIInvalidSourceFails(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "ffi-bad"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[ffi]
sources = ["src/ffi/missing.c"]
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	err = runBuildManifest(manifest, root)
	if err == nil {
		t.Fatal("expected error for missing FFI source")
	}
	if !strings.Contains(err.Error(), "missing.c") {
		t.Fatalf("error should mention missing file: %v", err)
	}

	// Build tools should NOT have been invoked
	if _, statErr := os.Stat(logPath); statErr == nil {
		t.Fatal("build tools were invoked despite invalid FFI config")
	}
}

func TestRunBuildManifestNoFFISkipsConfig(t *testing.T) {
	resetCommandGlobals(t)
	stubSeedBuiltImage(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	logPath := filepath.Join(t.TempDir(), "build.log")
	toolsDir := t.TempDir()
	writeFakeBuildTools(t, toolsDir, logPath)
	prependPath(t, toolsDir)

	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	withChdir(t, t.TempDir())
	writeManifestProject(t, projectRoot, `[project]
name = "no-ffi"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	if err := runBuildManifest(manifest, root); err != nil {
		t.Fatalf("runBuildManifest: %v", err)
	}

	// No config file should be generated
	configPath := filepath.Join(projectRoot, ".froth-build", "project_ffi.cmake")
	if _, err := os.Stat(configPath); !os.IsNotExist(err) {
		t.Fatal("project_ffi.cmake should not exist when no [ffi] declared")
	}

	// Build log should NOT contain FFI config arg
	log := mustReadFile(t, logPath)
	if strings.Contains(log, "FROTH_PROJECT_FFI_CONFIG") {
		t.Fatalf("build log should not contain FFI config arg:\n%s", log)
	}
}

func TestRunBuildLegacyCleanDeletesESPIDFBuildDir(t *testing.T) {
	resetCommandGlobals(t)
	cleanFlag = true
	targetFlag = "esp-idf"

	root := makeFakeKernelRoot(t)
	targetDir := filepath.Join(root, "targets", "esp-idf")
	mustWriteFile(t, filepath.Join(targetDir, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.16)\nproject(froth)\n")
	mustWriteFile(t, filepath.Join(targetDir, "build", "stale.txt"), "stale")

	logPath := filepath.Join(t.TempDir(), "idf.log")
	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "idf.py"), "#!/bin/sh\nprintf 'idf.py %s\\n' \"$*\" >> \""+logPath+"\"\nmkdir -p build\n: > build/rebuilt.txt\n")

	frothyHome := t.TempDir()
	exportPath := filepath.Join(frothyHome, "sdk", "esp-idf", "export.sh")
	mustWriteFile(t, exportPath, "export PATH=\""+toolsDir+":$PATH\"\n")
	t.Setenv("FROTHY_HOME", frothyHome)

	withChdir(t, root)

	var buildErr error
	stdout, _ := captureOutput(t, func() {
		buildErr = runBuildLegacy()
	})
	if buildErr != nil {
		t.Fatalf("runBuildLegacy: %v", buildErr)
	}

	if !strings.Contains(stdout, "Cleaned build directory") {
		t.Fatalf("stdout = %q, want clean confirmation", stdout)
	}
	if _, err := os.Stat(filepath.Join(targetDir, "build", "stale.txt")); !os.IsNotExist(err) {
		t.Fatalf("stale file still exists after clean build: %v", err)
	}
	if _, err := os.Stat(filepath.Join(targetDir, "build", "rebuilt.txt")); err != nil {
		t.Fatalf("rebuilt marker missing after idf.py build: %v", err)
	}

	log := mustReadFile(t, logPath)
	if !strings.Contains(log, "idf.py build") {
		t.Fatalf("idf log = %q, want idf.py build", log)
	}
}

func TestPatchESPIDFMainCMakePinsAbsoluteKernelRoot(t *testing.T) {
	stagedDir := t.TempDir()
	mainCMakePath := filepath.Join(stagedDir, "main", "CMakeLists.txt")
	mustWriteFile(t, mainCMakePath, `set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
include(${FROTH_ROOT}/cmake/froth_board_assets.cmake)
`)

	kernelRoot := filepath.Join(t.TempDir(), "kernel-root")
	if err := patchESPIDFMainCMake(stagedDir, kernelRoot); err != nil {
		t.Fatalf("patchESPIDFMainCMake: %v", err)
	}

	patched := mustReadFile(t, mainCMakePath)
	wantPrefix := fmt.Sprintf("set(FROTH_ROOT %q)", filepath.ToSlash(kernelRoot))
	if !strings.Contains(patched, wantPrefix) {
		t.Fatalf("patched cmake = %q, want %q", patched, wantPrefix)
	}
	if strings.Contains(patched, "${CMAKE_CURRENT_LIST_DIR}/../../..") {
		t.Fatalf("patched cmake still contains relative FROTH_ROOT: %q", patched)
	}
}

func makeFakeKernelRoot(t *testing.T) string {
	t.Helper()

	root := t.TempDir()
	mustWriteFile(t, filepath.Join(root, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.23)\nproject(Froth)\n")
	mustWriteFile(t, filepath.Join(root, "boards", "posix", "ffi.c"), "/* board */\n")
	mustWriteFile(t, filepath.Join(root, "boards", "esp32-devkit-v1", "ffi.c"), "/* board */\n")
	mustWriteFile(t, filepath.Join(root, "src", "froth_vm.h"), "/* vm */\n")
	return root
}

func writeManifestProject(t *testing.T, root string, manifest string) {
	t.Helper()
	mustWriteFile(t, filepath.Join(root, "froth.toml"), manifest)
}
