package cmd

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/froth/tools/cli/internal/project"
)

func TestRunDoctorWithoutManifestOutputsSystemChecksOnly(t *testing.T) {
	resetCommandGlobals(t)

	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "cmake"), "#!/bin/sh\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(toolsDir, "make"), "#!/bin/sh\nexit 0\n")
	prependPath(t, toolsDir)

	withChdir(t, t.TempDir())
	portFlag = "/dev/definitely-not-a-froth-device"

	stdout, _ := captureOutput(t, func() {
		if err := runDoctor(); err != nil {
			t.Fatalf("runDoctor: %v", err)
		}
	})

	if !strings.Contains(stdout, "go: ") {
		t.Fatalf("stdout = %q, want go version", stdout)
	}
	if !strings.Contains(stdout, "cmake: ") {
		t.Fatalf("stdout = %q, want cmake status", stdout)
	}
	if !strings.Contains(stdout, "make: ") {
		t.Fatalf("stdout = %q, want make status", stdout)
	}
	if strings.Contains(stdout, "project: ") {
		t.Fatalf("stdout = %q, want no project section", stdout)
	}
}

func TestRunProjectDoctorWithManifestOutputsProjectDetails(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	writeManifestProject(t, projectRoot, `[project]
name = "doctor-demo"
entry = "src/main.froth"

[target]
board = "esp32-devkit-v1"
platform = "esp-idf"

[dependencies]
stepper = { path = "lib/stepper.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")
	mustWriteFile(t, filepath.Join(projectRoot, "lib", "stepper.froth"), "stepper = 1\n")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	withChdir(t, t.TempDir())
	stdout, _ := captureOutput(t, func() {
		runProjectDoctor(manifest, root)
	})

	for _, want := range []string{
		"project: doctor-demo",
		"target: esp32-devkit-v1 (esp-idf)",
		"entry: src/main.froth",
		"dependency stepper: lib/stepper.froth",
		"board: " + filepath.Join(kernelRoot, "boards", "esp32-devkit-v1"),
	} {
		if !strings.Contains(stdout, want) {
			t.Fatalf("stdout = %q, want %q", stdout, want)
		}
	}
}

func TestRunProjectDoctorMissingDependencyShowsRemediation(t *testing.T) {
	resetCommandGlobals(t)

	projectRoot := t.TempDir()
	kernelRoot := makeFakeKernelRoot(t)
	oldEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) { return kernelRoot, nil }
	t.Cleanup(func() { ensureSDKRoot = oldEnsureSDKRoot })

	writeManifestProject(t, projectRoot, `[project]
name = "doctor-demo"
entry = "src/main.froth"

[dependencies]
stepper = { path = "lib/stepper.froth" }
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	manifest, root, err := project.Load(projectRoot)
	if err != nil {
		t.Fatalf("project.Load: %v", err)
	}
	withChdir(t, t.TempDir())
	stdout, _ := captureOutput(t, func() {
		runProjectDoctor(manifest, root)
	})

	if !strings.Contains(stdout, "dependency stepper: missing (lib/stepper.froth)") {
		t.Fatalf("stdout = %q, want missing dependency status", stdout)
	}
	if !strings.Contains(stdout, "Create `lib/stepper.froth` or remove `[dependencies].stepper` from `froth.toml`.") {
		t.Fatalf("stdout = %q, want remediation hint", stdout)
	}
}

func TestRunDoctorReportsGMakeAndOptionalTooling(t *testing.T) {
	resetCommandGlobals(t)

	toolsDir := t.TempDir()
	mustWriteExecutable(t, filepath.Join(toolsDir, "cmake"), "#!/bin/sh\nexit 0\n")
	mustWriteExecutable(t, filepath.Join(toolsDir, "gmake"), "#!/bin/sh\nexit 0\n")
	prependPath(t, toolsDir)

	oldDoctorLookPath := doctorLookPath
	doctorLookPath = func(name string) (string, error) {
		switch name {
		case "cmake", "gmake":
			return filepath.Join(toolsDir, name), nil
		case "make", "esptool.py", "esptool":
			return "", os.ErrNotExist
		default:
			return filepath.Join(toolsDir, name), nil
		}
	}
	t.Cleanup(func() { doctorLookPath = oldDoctorLookPath })

	t.Setenv("FROTH_HOME", filepath.Join(t.TempDir(), "froth-home"))
	withChdir(t, t.TempDir())
	portFlag = "/dev/definitely-not-a-froth-device"

	stdout, _ := captureOutput(t, func() {
		if err := runDoctor(); err != nil {
			t.Fatalf("runDoctor: %v", err)
		}
	})

	if !strings.Contains(stdout, "make: "+filepath.Join(toolsDir, "gmake")+" (via gmake)") {
		t.Fatalf("stdout = %q, want gmake path", stdout)
	}
	if !strings.Contains(stdout, "esp-idf: not found") || !strings.Contains(stdout, "Needed only for custom ESP32 builds.") {
		t.Fatalf("stdout = %q, want optional esp-idf note", stdout)
	}
	if !strings.Contains(stdout, "esptool: not found") || !strings.Contains(stdout, "Needed only for flashing ESP32 hardware.") {
		t.Fatalf("stdout = %q, want optional esptool note", stdout)
	}
}
