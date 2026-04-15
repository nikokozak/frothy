package cmd

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/nikokozak/frothy/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/frothy/tools/cli/internal/project"
	serialpkg "github.com/nikokozak/frothy/tools/cli/internal/serial"
)

var flashBuildManifest = runBuildManifest
var flashResolvePort = resolveFlashPort
var flashESPIDFDirFn = flashESPIDFDir
var flashApplyRuntime = applyRuntimeAfterFlash

func runFlash() error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err == nil {
		noteManifestSelectionOverride(manifest)
		return runFlashManifest(manifest, root)
	}
	if _, rootErr := project.FindProjectRoot(cwd); rootErr == nil {
		return err
	}

	if _, localErr := findLocalKernelRoot(); localErr == nil {
		return runFlashLegacy()
	}

	return fmt.Errorf("`%s flash` now supports source-based flashing only. Workshop boards are preflashed; for maintainer flashing, run from a Frothy project or repo checkout", cliCommandName)
}

func runFlashManifest(manifest *project.Manifest, root string) error {
	fmt.Printf("Building for %s...\n", manifest.Target.Board)
	if err := flashBuildManifest(manifest, root); err != nil {
		return err
	}

	switch manifest.Target.Platform {
	case "", "posix":
		fmt.Printf("binary: %s\n", filepath.Join(root, ".froth-build", "firmware", "Frothy"))
		return nil
	case "esp-idf":
		port, err := flashResolvePort()
		if err != nil {
			return err
		}
		fmt.Printf("Flashing to %s...\n", port)
		if err := flashESPIDFDirFn(filepath.Join(root, ".froth-build", "esp-idf"), port); err != nil {
			return err
		}
		runtimePath := filepath.Join(root, ".froth-build", "runtime.frothy")
		fmt.Printf("Applying runtime source: %s\n", runtimePath)
		return flashApplyRuntime(port, runtimePath)
	default:
		return fmt.Errorf("unknown target: %s", manifest.Target.Platform)
	}
}

func applyRuntimeAfterFlash(port string, runtimeSourcePath string) error {
	sourceBytes, err := os.ReadFile(runtimeSourcePath)
	if err != nil {
		return fmt.Errorf("read runtime source: %w", err)
	}

	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: port,
	})
	if _, err := connectFlashedRuntime(manager, 15*time.Second); err != nil {
		return fmt.Errorf("connect after flash: %w", err)
	}
	defer manager.Disconnect()

	if err := seedFrothyRuntime(manager, string(sourceBytes)); err != nil {
		return fmt.Errorf("apply runtime source: %w", err)
	}
	return nil
}

func connectFlashedRuntime(manager *frothycontrol.Manager, timeout time.Duration) (*frothycontrol.DeviceInfo, error) {
	deadline := time.Now().Add(timeout)
	var lastErr error

	for {
		info, err := manager.Connect("")
		if err == nil {
			return info, nil
		}

		lastErr = err
		var selectionErr *frothycontrol.ConnectSelectionError
		if errors.As(err, &selectionErr) && selectionErr.Code == "multiple_devices" {
			return nil, err
		}
		if time.Now().After(deadline) {
			return nil, lastErr
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func runFlashLegacy() error {
	root, err := findLocalKernelRoot()
	if err != nil {
		return err
	}

	selection, err := resolveLegacyCLISelection(root)
	if err != nil {
		return err
	}

	switch selection.Platform {
	case "posix":
		if err := cleanBuildDirForExplicitSelection(filepath.Join(root, "build"), selection); err != nil {
			return err
		}
		if err := buildPosix(root, selection.Board); err != nil {
			return err
		}
		fmt.Println("no flash step for POSIX target")
		fmt.Printf("binary: %s\n", filepath.Join(root, "build", "Frothy"))
		return nil
	case "esp-idf":
		if err := cleanBuildDirForExplicitSelection(filepath.Join(root, "targets", "esp-idf", "build"), selection); err != nil {
			return err
		}
		if err := buildESPIDF(root, selection.Board); err != nil {
			return err
		}
		return flashESPIDF(root)
	default:
		return fmt.Errorf("unknown target: %s", selection.Platform)
	}
}

func flashESPIDF(root string) error {
	targetDir := filepath.Join(root, "targets", "esp-idf")
	port, err := resolveFlashPort()
	if err != nil {
		return err
	}
	fmt.Printf("Flashing to %s...\n", port)
	return flashESPIDFDir(targetDir, port)
}

func resolveFlashPort() (string, error) {
	port := portFlag
	if port == "" {
		candidates, err := serialpkg.ListCandidates()
		if err != nil || len(candidates) == 0 {
			return "", fmt.Errorf("no serial port found (use --port)")
		}
		port = candidates[0]
		fmt.Printf("detected port: %s\n", port)
	}

	if !strings.HasPrefix(port, "/dev/") {
		return "", fmt.Errorf("invalid port path: %s", port)
	}

	return port, nil
}

func flashESPIDFDir(targetDir string, port string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	args := []string{
		"-c",
		`. "$IDF_EXPORT" && exec idf.py "$@"`,
		"bash",
		"flash",
		"-p",
		port,
	}

	cmd := exec.Command("bash", args...)
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py flash: %w", err)
	}

	return nil
}
