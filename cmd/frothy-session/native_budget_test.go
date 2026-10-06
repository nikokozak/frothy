package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func libWithNatives(name string, count int) resolvedLibrary {
	lib := resolvedLibrary{name: name}
	for i := 0; i < count; i++ {
		lib.natives = append(lib.natives, libraryNative{name: name + ".w", cFunction: "f"})
	}
	return lib
}

// stepper (15) and synth (8) cannot share a board with 21 free rows. The
// message names the board, the free rows, and each library that asks for them.
func TestCheckNativeBudgetRejectsLibrariesThatDoNotFit(t *testing.T) {
	budget := nativeBudget{baseRows: 142, tableSize: 163, libraryNames: 64}
	libs := []resolvedLibrary{libWithNatives("stepper", 15), libWithNatives("synth", 8)}
	want := "board esp32_devkit_v1 has 21 free native rows (the base uses 142 of 163) " +
		"but the libraries need 23 (stepper 15, synth 8); remove a library\n" +
		"note: a larger FR_PROFILE_NATIVE_TABLE_SIZE also fits them, but it changes " +
		"the profile hash, so saved programs must be sent again"
	err := checkNativeBudget("esp32_devkit_v1", budget, libs)
	if err == nil || err.Error() != want {
		t.Fatalf("error = %v, want %q", err, want)
	}
	if err := checkNativeBudget("esp32_devkit_v1", budget, libs[:1]); err != nil {
		t.Errorf("stepper alone should fit: %v", err)
	}
	// 73 natives also pass the 64 name records, so a larger table would not
	// fit them: no note about the table size.
	want73 := "board esp32_devkit_v1 has 21 free native rows (the base uses 142 of 163) " +
		"but the libraries need 73 (fat 73); remove a library"
	err = checkNativeBudget("esp32_devkit_v1", budget, []resolvedLibrary{libWithNatives("fat", 73)})
	if err == nil || err.Error() != want73 {
		t.Fatalf("error = %v, want %q", err, want73)
	}
}

// On the XIAO RP2040 the library name records (64) run out before the free
// native rows (72), so 65 natives must not pass.
func TestCheckNativeBudgetRejectsMoreLibraryNativesThanNameRecords(t *testing.T) {
	budget := nativeBudget{baseRows: 57, tableSize: 129, libraryNames: 64}
	want := "board seeed_xiao_rp2040 holds names for at most 64 library natives, " +
		"but the libraries need 65 (fat 65); remove a library"
	err := checkNativeBudget("seeed_xiao_rp2040", budget, []resolvedLibrary{libWithNatives("fat", 65)})
	if err == nil || err.Error() != want {
		t.Fatalf("error = %v, want %q", err, want)
	}
	if err := checkNativeBudget("seeed_xiao_rp2040", budget, []resolvedLibrary{libWithNatives("fat", 64)}); err != nil {
		t.Fatalf("64 natives should fit: %v", err)
	}
}

func TestVerifyNativeBudgetReadsTheBoardBudgetOnlyForNativeLibraries(t *testing.T) {
	old := readNativeBudgetFn
	defer func() { readNativeBudgetFn = old }()
	calls := 0
	gotComposition := ""
	readNativeBudgetFn = func(_, _, compositionH string) (nativeBudget, error) {
		calls++
		gotComposition = compositionH
		return nativeBudget{baseRows: 20, tableSize: 21, libraryNames: 64}, nil
	}
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv(frothySourceRootEnv, root)
	if err := verifyNativeBudget(t.TempDir(), "host", nil); err != nil || calls != 0 {
		t.Fatalf("no natives: err=%v calls=%d", err, calls)
	}

	dir := t.TempDir()
	composition := filepath.Join(buildOutputDir(dir, "host"), "composition.h")
	if err := os.MkdirAll(filepath.Dir(composition), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(composition, []byte("/* test */\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	err = verifyNativeBudget(dir, "host", []resolvedLibrary{libWithNatives("neopixel", 2)})
	if err == nil || !strings.Contains(err.Error(), "1 free native rows") || calls != 1 {
		t.Fatalf("want a budget error after one read, got err=%v calls=%d", err, calls)
	}
	if gotComposition != composition {
		t.Fatalf("probe got composition %q, want %q", gotComposition, composition)
	}
}

// --no-make returns after it writes the generated files, before the check.
func TestBuildNoMakeSkipsTheNativeBudget(t *testing.T) {
	old := readNativeBudgetFn
	defer func() { readNativeBudgetFn = old }()
	calls := 0
	readNativeBudgetFn = func(_, _, _ string) (nativeBudget, error) {
		calls++
		return nativeBudget{baseRows: 21, tableSize: 21}, nil
	}
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
	if calls != 0 {
		t.Fatalf("--no-make read the native budget %d times", calls)
	}
}
