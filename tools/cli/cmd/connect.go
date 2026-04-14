package cmd

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

var connectLookPath = exec.LookPath

func runConnect(args []string) error {
	local := false

	for _, arg := range args {
		switch arg {
		case "--local":
			local = true
		default:
			return fmt.Errorf("unknown connect flag: %s", arg)
		}
	}

	if local && portFlag != "" {
		return fmt.Errorf("--local cannot be combined with --port")
	}

	if local {
		return runConnectLocal()
	}
	return runConnectSerial()
}

func runConnectSerial() error {
	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: portFlag,
	})
	info, err := manager.Connect("")
	if err != nil {
		return err
	}
	defer manager.Disconnect()

	fmt.Printf("%s\n", formatConnectedMessage(info.Board, info.Port))
	return runConnectLoop(manager)
}

func runConnectLocal() error {
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return err
	}

	buildDir, binaryPath, err := localConnectPaths()
	if err != nil {
		return err
	}

	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		return fmt.Errorf("create local build dir: %w", err)
	}

	needsBuild, err := localBinaryNeedsBuild(binaryPath, kernelRoot)
	if err != nil {
		return err
	}
	if needsBuild {
		if err := buildLocalConnectBinary(buildDir, kernelRoot); err != nil {
			return err
		}
	}

	runDir, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		LocalRuntimePath: binaryPath,
		LocalRuntimeDir:  runDir,
	})
	info, err := manager.Connect("")
	if err != nil {
		return err
	}
	defer manager.Disconnect()

	fmt.Printf("%s\n", formatConnectedMessage(info.Board, "local"))
	return runConnectLoop(manager)
}

func runConnectLoop(manager *frothycontrol.Manager) error {
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Buffer(make([]byte, 0, 1024), 1024*1024)

	for {
		fmt.Print("frothy> ")
		if !scanner.Scan() {
			fmt.Println()
			return scanner.Err()
		}

		line := scanner.Text()
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		if trimmed == "quit" || trimmed == `\ quit` {
			return nil
		}

		value, err := runControlEval(manager, line)
		if err != nil {
			if isInterrupted(err) {
				fmt.Println()
				continue
			}
			fmt.Printf("eval: %v\n", err)
			continue
		}
		printControlValue(value)
	}
}

func formatConnectedMessage(board string, port string) string {
	if board == "" && port == "" {
		return "Connected"
	}
	if board == "" {
		return fmt.Sprintf("Connected on %s", port)
	}
	if port == "" {
		return fmt.Sprintf("Connected to %s", board)
	}
	return fmt.Sprintf("Connected to %s on %s", board, port)
}

func localConnectPaths() (string, string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", "", err
	}

	buildDir := filepath.Join(home, "frothy-local-build")
	return buildDir, filepath.Join(buildDir, "Frothy"), nil
}

func buildLocalConnectBinary(buildDir string, kernelRoot string) error {
	cmakePath, err := connectLookPath("cmake")
	if err != nil {
		return fmt.Errorf("cmake is required for 'froth connect --local'; install CMake and try again")
	}

	makePath, err := connectLookPath("make")
	if err != nil {
		makePath, _, err = findMakeTool(connectLookPath)
	}
	if err != nil {
		return fmt.Errorf("make is required for 'froth connect --local'; install GNU Make and ensure `make` or `gmake` is on PATH")
	}

	if err := runQuietBuildCommand(buildDir, cmakePath,
		kernelRoot,
		"-DFROTH_CELL_SIZE_BITS=32",
		"-DFROTH_BOARD=posix",
		"-DFROTH_PLATFORM=posix",
		"-DFROTHY_BUILD_HOST=ON",
	); err != nil {
		return fmt.Errorf("cmake configure failed: %w", err)
	}

	makeArgs := []string{}
	if jobs := runtime.NumCPU(); jobs > 1 {
		makeArgs = append(makeArgs, fmt.Sprintf("-j%d", jobs))
	}
	if err := runQuietBuildCommand(buildDir, makePath, makeArgs...); err != nil {
		return fmt.Errorf("make failed: %w", err)
	}

	return nil
}

func runQuietBuildCommand(dir string, name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir

	output, err := cmd.CombinedOutput()
	if err == nil {
		return nil
	}

	text := strings.TrimSpace(string(output))
	if text == "" {
		return err
	}

	return fmt.Errorf("%w\n%s", err, text)
}

func localBinaryNeedsBuild(binaryPath string, kernelRoot string) (bool, error) {
	binaryInfo, err := os.Stat(binaryPath)
	if err != nil {
		if os.IsNotExist(err) {
			return true, nil
		}
		return false, fmt.Errorf("stat local Frothy binary: %w", err)
	}

	latestInput, err := latestLocalBuildInputModTime(kernelRoot)
	if err != nil {
		return false, err
	}

	return binaryInfo.ModTime().Before(latestInput), nil
}

func latestLocalBuildInputModTime(kernelRoot string) (time.Time, error) {
	paths := []string{
		"CMakeLists.txt",
		"boards",
		"cmake",
		"platforms",
		"src",
	}

	var latest time.Time
	for _, rel := range paths {
		path := filepath.Join(kernelRoot, rel)
		info, err := os.Stat(path)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return time.Time{}, fmt.Errorf("stat %s: %w", path, err)
		}

		if info.IsDir() {
			err = filepath.Walk(path, func(walkPath string, walkInfo os.FileInfo, walkErr error) error {
				if walkErr != nil {
					return walkErr
				}
				if walkInfo.IsDir() || !walkInfo.Mode().IsRegular() {
					return nil
				}
				if walkInfo.ModTime().After(latest) {
					latest = walkInfo.ModTime()
				}
				return nil
			})
			if err != nil {
				return time.Time{}, fmt.Errorf("walk %s: %w", path, err)
			}
			continue
		}

		if info.Mode().IsRegular() && info.ModTime().After(latest) {
			latest = info.ModTime()
		}
	}

	if latest.IsZero() {
		return time.Time{}, fmt.Errorf("no local POSIX build inputs found under %s", kernelRoot)
	}

	return latest, nil
}
