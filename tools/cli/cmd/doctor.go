package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
)

var doctorLookPath = exec.LookPath

func runDoctor() error {
	fmt.Printf("go: %s\n", runtime.Version())

	if path, err := doctorLookPath("cmake"); err == nil {
		fmt.Printf("cmake: %s\n", path)
	} else {
		doctorFailure("cmake", "not found", "Install CMake and ensure `cmake` is on PATH.")
	}

	if path, name, err := findMakeTool(doctorLookPath); err == nil {
		if name == "gmake" {
			fmt.Printf("make: %s (via gmake)\n", path)
		} else {
			fmt.Printf("make: %s\n", path)
		}
	} else {
		doctorFailure("make", "not found", "Install GNU Make and ensure `make` or `gmake` is on PATH.")
	}

	candidates, err := serialpkg.ListCandidates()
	if err != nil {
		doctorFailure("serial", fmt.Sprintf("error: %v", err), "Check USB permissions and retry `froth doctor`.")
	} else if len(candidates) == 0 {
		doctorFailure("serial", "no USB-serial ports found", "Connect a USB-serial device and retry.")
	} else {
		fmt.Printf("serial: %d port(s)\n", len(candidates))
		for _, p := range candidates {
			fmt.Printf("  %s\n", p)
		}
	}

	if exportPath, ok := doctorESPIDFStatus(); ok {
		fmt.Printf("esp-idf: installed (%s)\n", filepath.Dir(exportPath))
	} else {
		doctorOptional("esp-idf", "not found", "Needed only for custom ESP32 builds. Install with `froth setup esp-idf`.")
	}

	if path, err := doctorLookPath("esptool.py"); err == nil {
		fmt.Printf("esptool: %s\n", path)
	} else if path, err := doctorLookPath("esptool"); err == nil {
		fmt.Printf("esptool: %s\n", path)
	} else {
		doctorOptional("esptool", "not found", "Needed only for flashing ESP32 hardware. Install with `brew install esptool` or `pip install esptool`.")
	}

	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err == nil {
		runProjectDoctor(manifest, root)
	} else if !isBareProjectMode(err) {
		doctorFailure("project", fmt.Sprintf("error: %v", err), "Fix `froth.toml` and retry `froth doctor`.")
	}

	probePort, probeErr := doctorProbePort(candidates)
	if probeErr != nil {
		doctorFailure("device", "not reachable", doctorDeviceRemediation())
		doctorFailure("probe", probeErr.Error(), doctorDeviceRemediation())
	} else {
		manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
			DefaultPort: probePort,
		})
		info, err := manager.Connect("")
		if err != nil {
			doctorFailure("device", "not reachable", doctorDeviceRemediation())
			doctorFailure("probe", fmt.Sprintf("%s: %v", probePort, err), doctorDeviceRemediation())
		} else {
			fmt.Printf("device: %s on %s (%d-bit)\n", info.Version, info.Board, info.CellBits)
			_ = manager.Disconnect()
		}
	}

	return nil
}

func doctorProbePort(candidates []string) (string, error) {
	if portFlag != "" {
		return portFlag, nil
	}
	switch len(candidates) {
	case 0:
		return "", fmt.Errorf("no USB-serial ports available for probe")
	case 1:
		return candidates[0], nil
	default:
		return "", fmt.Errorf("multiple candidate ports found; probe one with --port <path>")
	}
}

func runProjectDoctor(manifest *project.Manifest, root string) {
	fmt.Printf("project: %s\n", manifest.Project.Name)
	fmt.Printf("target: %s (%s)\n", manifest.Target.Board, manifest.Target.Platform)

	entryPath := filepath.Join(root, manifest.Project.Entry)
	if _, err := os.Stat(entryPath); err == nil {
		fmt.Printf("entry: %s\n", manifest.Project.Entry)
	} else {
		doctorFailure("entry", fmt.Sprintf("missing (%s)", manifest.Project.Entry), fmt.Sprintf("Create `%s` or update `[project].entry` in `froth.toml`.", manifest.Project.Entry))
	}

	depNames := make([]string, 0, len(manifest.Dependencies))
	for name := range manifest.Dependencies {
		depNames = append(depNames, name)
	}
	sort.Strings(depNames)

	for _, name := range depNames {
		dep := manifest.Dependencies[name]
		label := fmt.Sprintf("dependency %s", name)
		if dep.Path == "" {
			doctorFailure(label, "empty path", fmt.Sprintf("Set `[dependencies].%s.path` in `froth.toml` or remove `[dependencies].%s`.", name, name))
			continue
		}

		depPath := filepath.Join(root, dep.Path)
		info, err := os.Stat(depPath)
		if err != nil {
			doctorFailure(label, fmt.Sprintf("missing (%s)", dep.Path), fmt.Sprintf("Create `%s` or remove `[dependencies].%s` from `froth.toml`.", dep.Path, name))
			continue
		}

		if info.IsDir() {
			initPath := filepath.Join(depPath, "init.froth")
			if _, err := os.Stat(initPath); err != nil {
				doctorFailure(label, fmt.Sprintf("missing init.froth (%s)", dep.Path), fmt.Sprintf("Create `%s` or point `[dependencies].%s.path` at a file.", filepath.Join(dep.Path, "init.froth"), name))
				continue
			}
		}

		fmt.Printf("%s: %s\n", label, dep.Path)
	}

	kernelRoot, err := findKernelRoot()
	if err != nil {
		doctorFailure("board", "sdk not available", "Run `froth build` to extract the embedded SDK, then retry `froth doctor`.")
		return
	}

	boardDir := filepath.Join(kernelRoot, "boards", manifest.Target.Board)
	if _, err := os.Stat(boardDir); err == nil {
		fmt.Printf("board: %s\n", boardDir)
	} else {
		doctorFailure("board", fmt.Sprintf("missing (%s)", filepath.Join("boards", manifest.Target.Board)), fmt.Sprintf("Set `[target].board` in `froth.toml` to a directory that exists under `%s`.", filepath.Join(kernelRoot, "boards")))
	}
}

func doctorESPIDFStatus() (string, bool) {
	home, err := sdk.FrothHome()
	if err != nil || home == "" {
		return "", false
	}

	exportPath := filepath.Join(home, "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(exportPath); err != nil {
		return "", false
	}

	return exportPath, true
}

func doctorFailure(label string, status string, remediation string) {
	fmt.Printf("%s: %s\n", label, status)
	fmt.Printf("  fix: %s\n", remediation)
}

func doctorOptional(label string, status string, note string) {
	fmt.Printf("%s: %s\n", label, status)
	fmt.Printf("  note: %s\n", note)
}

func doctorDeviceRemediation() string {
	if portFlag != "" {
		return fmt.Sprintf("Check the device on `%s` and retry `froth doctor --port %s`.", portFlag, portFlag)
	}
	return "Connect a Frothy device or retry with `froth doctor --port <path>`."
}

func isBareProjectMode(err error) bool {
	return strings.Contains(err.Error(), "not in a Frothy project")
}
