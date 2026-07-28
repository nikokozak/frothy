package main

import (
	"bytes"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// makeTempProject builds a directory tree that frothy build can run
// against. Caller adds libs via libsContent.
func makeTempProject(t *testing.T, projectToml, mainFr string, libsContent map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "frothy.toml"), projectToml)
	if mainFr != "" {
		writeFile(t, filepath.Join(dir, "main.fr"), mainFr)
	}
	for relPath, content := range libsContent {
		writeFile(t, filepath.Join(dir, relPath), content)
	}
	return dir
}

func TestRunBuild_PureModulesLibrary(t *testing.T) {
	dir := makeTempProject(t, `name = "blink"
board = "host"

[deps]
servo = { path = "libs/servo" }
`, "servo.attach: 5\n", map[string]string{
		"libs/servo/lib.fr": "to servo.attach with pin [ pin ]\n",
	})
	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	// generator outputs present
	out := filepath.Join(dir, ".frothy", "build", "host")
	for _, f := range []string{"libs.cmake", "lib_natives.c", "library.fr", "main.fr"} {
		if _, err := os.Stat(filepath.Join(out, f)); err != nil {
			t.Errorf("missing output %s: %v", f, err)
		}
	}
	if _, err := os.Stat(filepath.Join(out, "program.fr")); err == nil {
		t.Errorf("program.fr should no longer be emitted")
	}
	// pure-modules library should leave lib_natives.c with the no-natives marker
	content, _ := os.ReadFile(filepath.Join(out, "lib_natives.c"))
	if !strings.Contains(string(content), "No library natives.") {
		t.Errorf("expected no-natives marker in lib_natives.c; got: %s", content)
	}
	// library.fr carries lib.fr content; main.fr carries main.fr content
	library, _ := os.ReadFile(filepath.Join(out, "library.fr"))
	if !strings.Contains(string(library), "to servo.attach with pin") {
		t.Errorf("library.fr missing library word: %s", library)
	}
	if strings.Contains(string(library), "servo.attach: 5") {
		t.Errorf("library.fr should not carry main.fr content: %s", library)
	}
	mainOut, _ := os.ReadFile(filepath.Join(out, "main.fr"))
	if !strings.Contains(string(mainOut), "servo.attach: 5") {
		t.Errorf("main.fr missing main.fr content: %s", mainOut)
	}
	if strings.Contains(string(mainOut), "to servo.attach with pin") {
		t.Errorf("main.fr should not carry library word: %s", mainOut)
	}
}

func TestRunBuild_MixedLibraryEmitsNatives(t *testing.T) {
	dir := makeTempProject(t, `name = "stage"
board = "host"

[deps]
neopixel = { path = "libs/neopixel" }
`, "neopixel.show:\n", map[string]string{
		"libs/neopixel/lib.fr":            "to neopixel.use [ ]\n",
		"libs/neopixel/native/neopixel.c": "/* extension */\n",
		"libs/neopixel/lib.toml": `name = "neopixel"
boards = ["host"]

[extension]
sources = ["native/neopixel.c"]

[[natives]]
name = "neopixel.show"
arity = 1
c_function = "fr_lib_neopixel_show"
`,
	})
	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	out := filepath.Join(dir, ".frothy", "build", "host")
	cmake, _ := os.ReadFile(filepath.Join(out, "libs.cmake"))
	if !strings.Contains(string(cmake), "neopixel/native/neopixel.c") {
		t.Errorf("libs.cmake missing extension source: %s", cmake)
	}
	natives, _ := os.ReadFile(filepath.Join(out, "lib_natives.c"))
	for _, want := range []string{
		`extern fr_err_t fr_lib_neopixel_show(`,
		`"neopixel.show", fr_lib_neopixel_show, 1`,
		`fr_lib_natives_count = 1`,
	} {
		if !strings.Contains(string(natives), want) {
			t.Errorf("lib_natives.c missing %q", want)
		}
	}
}

func TestRunBuild_BoardGateFailure(t *testing.T) {
	dir := makeTempProject(t, `name = "blink"
board = "atmega328p"

[deps]
neopixel = { path = "libs/neopixel" }
`, "", map[string]string{
		"libs/neopixel/lib.fr":            "",
		"libs/neopixel/native/neopixel.c": "",
		"libs/neopixel/lib.toml": `name = "neopixel"
boards = ["esp32_devkit_v1"]

[extension]
sources = ["native/neopixel.c"]
`,
	})
	var stdout, stderr bytes.Buffer
	err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr)
	if err == nil {
		t.Fatal("expected board gate to fail")
	}
	want := "library neopixel does not support board atmega328p"
	if !strings.Contains(err.Error(), want) {
		t.Fatalf("got %q, want it to contain %q", err.Error(), want)
	}
}

func TestRunBuild_LibraryToLibraryDep(t *testing.T) {
	dir := makeTempProject(t, `name = "show"
board = "host"

[deps]
stage = { path = "libs/stage" }
`, "", map[string]string{
		"libs/servo/lib.fr": "to servo.attach [ ]\n",
		"libs/stage/lib.fr": "to stage.go [ ]\n",
		"libs/stage/lib.toml": `name = "stage"
boards = ["host"]

[deps]
servo = { path = "../servo" }
`,
	})
	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	// library.fr should have servo's words before stage's: dependency
	// before dependent. Look for the body of each.
	library, _ := os.ReadFile(filepath.Join(dir, ".frothy", "build", "host", "library.fr"))
	svIdx := strings.Index(string(library), "servo.attach")
	stIdx := strings.Index(string(library), "stage.go")
	if svIdx < 0 || stIdx < 0 {
		t.Fatalf("missing library words in library.fr: %s", library)
	}
	if svIdx >= stIdx {
		t.Fatalf("servo should appear before stage; got svIdx=%d stIdx=%d", svIdx, stIdx)
	}
}

func TestRunBuild_IncludeResolved(t *testing.T) {
	dir := makeTempProject(t, `name = "blink"
board = "host"

[deps]
math = { path = "libs/math" }
`, "", map[string]string{
		"libs/math/lib.fr":     "include \"helpers.fr\"\nto math.use [ math.double: 21 ]\n",
		"libs/math/helpers.fr": "to math.double with n [ n n + ]\n",
	})
	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	library, _ := os.ReadFile(filepath.Join(dir, ".frothy", "build", "host", "library.fr"))
	if !strings.Contains(string(library), "math.double with n") {
		t.Errorf("include not preprocessed; library.fr: %s", library)
	}
}

func TestRunBuild_DropsStaleProgramFr(t *testing.T) {
	dir := makeTempProject(t, `name = "blink"
board = "host"

[deps]
servo = { path = "libs/servo" }
`, "servo.attach: 5\n", map[string]string{
		"libs/servo/lib.fr": "to servo.attach with pin [ pin ]\n",
	})
	out := filepath.Join(dir, ".frothy", "build", "host")
	if err := os.MkdirAll(out, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	stale := filepath.Join(out, "program.fr")
	writeFile(t, stale, "stale pre-D9 artifact\n")
	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: dir, skipMake: true}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	if _, err := os.Stat(stale); !os.IsNotExist(err) {
		t.Errorf("expected stale program.fr to be removed; stat err=%v", err)
	}
}

func TestRunBuildCommand_MissingManifest(t *testing.T) {
	dir := t.TempDir()
	var stdout, stderr bytes.Buffer
	rc := runBuildCommand([]string{"--project", dir, "--no-make"}, &stdout, &stderr)
	if rc == 0 {
		t.Fatal("expected nonzero exit code on missing frothy.toml")
	}
	if !strings.Contains(stderr.String(), "frothy.toml") {
		t.Fatalf("expected stderr to mention frothy.toml; got: %s", stderr.String())
	}
}

func TestBuildEmitContractRequiresFirmwareBuild(t *testing.T) {
	var stderr bytes.Buffer
	code := runBuildCommand([]string{
		"--project", t.TempDir(),
		"--no-make",
		"--emit-contract", filepath.Join(t.TempDir(), "contract.json"),
	}, io.Discard, &stderr)
	if code != 2 || !strings.Contains(stderr.String(),
		"--emit-contract cannot be combined with --no-make") {
		t.Fatalf("exit/stderr = %d/%q", code, stderr.String())
	}
}

func TestBuildEmitsContractOnlyAfterSuccessfulMake(t *testing.T) {
	if os.PathSeparator == '\\' {
		t.Skip("test uses a POSIX make stub")
	}
	sourceRoot, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	realMake, err := exec.LookPath("make")
	if err != nil {
		t.Fatal(err)
	}
	project := makeTempProject(t, "name = \"contract\"\nboard = \"host\"\n", "", nil)
	t.Setenv(frothySourceRootEnv, sourceRoot)
	binDir := t.TempDir()
	makePath := filepath.Join(binDir, "make")
	writeFile(t, makePath, "#!/bin/sh\n"+
		"case \" $* \" in\n"+
		"  *\" print-target-facts \"*) exec \""+
		strings.ReplaceAll(realMake, "\"", "\\\"")+"\" \"$@\" ;;\n"+
		"esac\n"+
		"exit \"${FROTHY_TEST_MAKE_EXIT:-0}\"\n")
	if err := os.Chmod(makePath, 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", binDir+string(os.PathListSeparator)+os.Getenv("PATH"))

	contractPath := filepath.Join(project, "target-contract.json")
	writeFile(t, contractPath, "stale\n")
	t.Setenv("FROTHY_TEST_MAKE_EXIT", "7")
	if err := runBuild(buildOptions{
		projectDir: project, contractOut: contractPath,
	}, io.Discard, io.Discard); err == nil {
		t.Fatal("failed make accepted")
	}
	if _, err := os.Stat(contractPath); !os.IsNotExist(err) {
		t.Fatalf("stale contract survived failed build: %v", err)
	}

	t.Setenv("FROTHY_TEST_MAKE_EXIT", "0")
	if err := runBuild(buildOptions{
		projectDir: project, contractOut: contractPath,
	}, io.Discard, io.Discard); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(contractPath)
	if err != nil {
		t.Fatal(err)
	}
	var contract publishedTargetContract
	if err := json.Unmarshal(data, &contract); err != nil {
		t.Fatal(err)
	}
	if contract.Schema != targetContractSchema || contract.Board != "host" ||
		len(contract.Capabilities) != len(capabilities) {
		t.Fatalf("published contract = %+v", contract)
	}
}

func TestRunBuild_UsesSourceRootOutsideProject(t *testing.T) {
	if os.PathSeparator == '\\' {
		t.Skip("test uses a POSIX make stub")
	}
	project := makeTempProject(t, "name = \"outside\"\nboard = \"host\"\n", "", nil)
	writeFile(t, filepath.Join(project, "Makefile"), "must not be used\n")
	sourceRoot := makeSourceRoot(t)
	t.Setenv(frothySourceRootEnv, sourceRoot)

	binDir := t.TempDir()
	makePath := filepath.Join(binDir, "make")
	writeFile(t, makePath, "#!/bin/sh\nprintf '%s\\n' \"$@\"\n")
	if err := os.Chmod(makePath, 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", binDir+string(os.PathListSeparator)+os.Getenv("PATH"))

	var stdout, stderr bytes.Buffer
	if err := runBuild(buildOptions{projectDir: project}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	want := "-C\n" + sourceRoot + "\nartifacts\nBOARD=host\n"
	if !strings.Contains(stdout.String(), want) {
		t.Fatalf("make argv = %q, want prefix %q", stdout.String(), want)
	}
	if strings.Contains(stdout.String(), "-C\n"+project+"\n") {
		t.Fatalf("make used project-local Makefile: %q", stdout.String())
	}
}

func compositionOutDir(dir string) string {
	return filepath.Join(dir, ".frothy", "build", "esp32_devkit_v1")
}

func TestRunBuild_PassesCompositionVarsToMake(t *testing.T) {
	if os.PathSeparator == '\\' {
		t.Skip("test uses a POSIX make stub")
	}
	sourceRoot, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	realMake, err := exec.LookPath("make")
	if err != nil {
		t.Fatal(err)
	}
	project := makeTempProject(t,
		"name = \"comp\"\nboard = \"host\"\n\n[capabilities]\nble = false\n", "", nil)
	t.Setenv(frothySourceRootEnv, sourceRoot)
	binDir := t.TempDir()
	makePath := filepath.Join(binDir, "make")
	writeFile(t, makePath, "#!/bin/sh\n"+
		"case \" $* \" in\n"+
		"  *\" print-target-facts \"*) exec \""+
		strings.ReplaceAll(realMake, "\"", "\\\"")+"\" \"$@\" ;;\n"+
		"esac\n"+
		"printf '%s\\n' \"$@\"\n")
	if err := os.Chmod(makePath, 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", binDir+string(os.PathListSeparator)+os.Getenv("PATH"))

	var stdout, stderr bytes.Buffer
	if err := runBuild(
		buildOptions{projectDir: project}, &stdout, &stderr); err != nil {
		t.Fatalf("runBuild: %v\nstderr: %s", err, stderr.String())
	}
	out := filepath.Join(project, ".frothy", "build", "host")
	for _, want := range []string{
		"FROTHY_COMPOSITION_H=" + filepath.Join(out, "composition.h"),
		"FROTHY_COMPOSITION_SDKCONFIG=" + filepath.Join(out, "composition.sdkconfig"),
	} {
		if !strings.Contains(stdout.String(), want) {
			t.Fatalf("make argv missing %q; got:\n%s", want, stdout.String())
		}
	}
}

func TestBuildEmitsCompositionFiles(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv(frothySourceRootEnv, root)
	dir := makeTempProject(t,
		"name = \"comp\"\nboard = \"esp32_devkit_v1\"\n\n[capabilities]\nble = false\n", "", nil)
	if rc := runBuildCommand([]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &bytes.Buffer{}); rc != 0 {
		t.Fatalf("build exited %d", rc)
	}
	h, err := os.ReadFile(filepath.Join(compositionOutDir(dir), "composition.h"))
	if err != nil || !strings.Contains(string(h), "#define FR_FEATURE_BLE 0") {
		t.Fatalf("composition.h missing ble-off define: %v\n%s", err, h)
	}
	sdk, err := os.ReadFile(filepath.Join(compositionOutDir(dir), "composition.sdkconfig"))
	if err != nil || !strings.Contains(string(sdk), "CONFIG_BT_ENABLED=n") {
		t.Fatalf("composition.sdkconfig missing BT-off: %v\n%s", err, sdk)
	}
}

func TestBuildDefaultCompositionRemovesStaleFiles(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv(frothySourceRootEnv, root)
	dir := makeTempProject(t,
		"name = \"comp\"\nboard = \"esp32_devkit_v1\"\n\n[capabilities]\nble = false\n", "", nil)
	if rc := runBuildCommand([]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &bytes.Buffer{}); rc != 0 {
		t.Fatal("composed build failed")
	}
	// Rewrite the manifest back to profile defaults (no [capabilities]).
	writeFile(t, filepath.Join(dir, "frothy.toml"), "name = \"comp\"\nboard = \"esp32_devkit_v1\"\n")
	if rc := runBuildCommand([]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &bytes.Buffer{}); rc != 0 {
		t.Fatal("default build failed")
	}
	for _, f := range []string{"composition.h", "composition.sdkconfig"} {
		if _, err := os.Stat(filepath.Join(compositionOutDir(dir), f)); !os.IsNotExist(err) {
			t.Fatalf("stale %s survived a default build (err=%v)", f, err)
		}
	}
}

func TestBuildRejectsUnknownCapability(t *testing.T) {
	dir := makeTempProject(t,
		"name = \"comp\"\nboard = \"esp32_devkit_v1\"\n\n[capabilities]\nwarp = false\n", "", nil)
	var stderr bytes.Buffer
	if rc := runBuildCommand([]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &stderr); rc == 0 {
		t.Fatal("expected nonzero exit for unknown capability")
	} else if !strings.Contains(stderr.String(), "unknown capability") {
		t.Fatalf("expected unknown-capability error, got: %s", stderr.String())
	}
}

func TestBuildRejectsUnavailableEnabledCapability(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv(frothySourceRootEnv, root)
	dir := makeTempProject(t,
		"name = \"comp\"\nboard = \"seeed_xiao_rp2040\"\n\n[capabilities]\nble = true\n", "", nil)
	var stderr bytes.Buffer
	if rc := runBuildCommand(
		[]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &stderr); rc == 0 {
		t.Fatal("expected nonzero exit for unavailable BLE")
	} else if !strings.Contains(stderr.String(),
		`capability "ble" is unavailable on board seeed_xiao_rp2040: hardware_absent`) {
		t.Fatalf("expected hardware-absence error, got: %s", stderr.String())
	}
}

func TestBuildGatesLibrariesWithResolvedCapabilities(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv(frothySourceRootEnv, root)
	tests := []struct {
		board   string
		wantErr string
	}{
		{board: "host", wantErr: "profile_disabled"},
		{board: "esp32_devkit_v1"},
	}
	for _, test := range tests {
		t.Run(test.board, func(t *testing.T) {
			dir := makeTempProject(t,
				"name = \"contract\"\nboard = \""+test.board+"\"\n\n"+
					"[deps]\nsteps = { path = \"libs/steps\" }\n", "",
				map[string]string{
					"libs/steps/lib.fr": "",
					"libs/steps/lib.toml": "name = \"steps\"\nboards = [\"" +
						test.board + "\"]\nrequires = [\"cells\"]\n",
				})
			var stderr bytes.Buffer
			rc := runBuildCommand(
				[]string{"--project", dir, "--no-make"}, &bytes.Buffer{}, &stderr)
			if test.wantErr == "" && rc != 0 {
				t.Fatalf("build exited %d: %s", rc, stderr.String())
			}
			if test.wantErr != "" &&
				(rc == 0 || !strings.Contains(stderr.String(), test.wantErr)) {
				t.Fatalf("build exit/error = %d/%q, want %s",
					rc, stderr.String(), test.wantErr)
			}
		})
	}
}

func TestWriteFileIfChangedSkipsIdenticalContent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "composition.sdkconfig")
	content := []byte("# Generated by frothy build. Do not edit.\nCONFIG_BT_ENABLED=n\n")
	if err := writeFileIfChanged(path, content); err != nil {
		t.Fatal(err)
	}
	before, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	time.Sleep(10 * time.Millisecond)
	if err := writeFileIfChanged(path, content); err != nil {
		t.Fatal(err)
	}
	after, _ := os.Stat(path)
	if !after.ModTime().Equal(before.ModTime()) {
		t.Fatal("identical content was rewritten (would churn the esp-idf configure step)")
	}
	time.Sleep(10 * time.Millisecond)
	if err := writeFileIfChanged(path, []byte("# changed\nCONFIG_BT_ENABLED=y\n")); err != nil {
		t.Fatal(err)
	}
	changed, _ := os.Stat(path)
	if changed.ModTime().Equal(before.ModTime()) {
		t.Fatal("changed content was not rewritten")
	}
}
