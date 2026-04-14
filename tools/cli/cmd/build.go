package cmd

import (
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

var ensureSDKRoot = sdk.EnsureSDK
var buildLookPath = exec.LookPath
var seedBuiltImage = seedFrothyImage

func runBuild() error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	// Try manifest-driven build first
	manifest, root, err := project.Load(cwd)
	if err != nil {
		// Fall back to legacy project detection (kernel repo)
		return runBuildLegacy()
	}
	if targetFlag != "" {
		return fmt.Errorf("`froth build` does not accept --target inside a project; edit [target] in froth.toml")
	}

	if err := cleanBuildDirIfRequested(filepath.Join(root, ".froth-build")); err != nil {
		return err
	}

	return runBuildManifest(manifest, root)
}

func runBuildManifest(manifest *project.Manifest, root string) error {
	if err := cleanBuildDirIfRequested(filepath.Join(root, ".froth-build")); err != nil {
		return err
	}

	// Resolve includes
	result, err := project.Resolve(manifest, root)
	if err != nil {
		return err
	}

	for _, w := range result.Warnings {
		fmt.Fprintf(os.Stderr, "warning: %s\n", w)
	}

	if len(result.Files) > 1 {
		fmt.Fprintf(os.Stderr, "Resolved %s (%d dependencies)\n",
			result.Files[len(result.Files)-1], len(result.Files)-1)
	}

	// Write resolved source to .froth-build/
	buildDir := filepath.Join(root, ".froth-build")
	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return fmt.Errorf("create build dir: %w", err)
	}

	resolvedPath := filepath.Join(buildDir, "resolved.froth")
	if err := os.WriteFile(resolvedPath, []byte(result.Source), 0644); err != nil {
		return fmt.Errorf("write resolved source: %w", err)
	}
	runtimeSource := project.StripBoundaryMarkers(result.Source)
	runtimePath := filepath.Join(buildDir, "runtime.frothy")
	if err := os.WriteFile(runtimePath, []byte(runtimeSource), 0644); err != nil {
		return fmt.Errorf("write runtime source: %w", err)
	}

	// Prepare project FFI config (if [ffi] is declared)
	ffiConfigPath, err := prepareProjectFFIConfig(manifest, root, buildDir)
	if err != nil {
		return err
	}

	// Build based on target platform
	switch manifest.Target.Platform {
	case "posix", "":
		return buildPosixManifest(manifest, root, runtimePath, ffiConfigPath)
	case "esp-idf":
		return buildESPIDFManifest(manifest, root, runtimePath, ffiConfigPath)
	default:
		return fmt.Errorf("unknown platform: %s", manifest.Target.Platform)
	}
}

// prepareProjectFFIConfig resolves [ffi] from the manifest and writes a
// CMake config fragment to .froth-build/project_ffi.cmake. Returns the
// absolute path to the generated file, or "" if no project FFI is declared.
func prepareProjectFFIConfig(manifest *project.Manifest, root string, buildDir string) (string, error) {
	resolved, err := project.ResolveFFI(manifest, root)
	if err != nil {
		return "", fmt.Errorf("project ffi: %w", err)
	}
	if resolved == nil {
		return "", nil
	}

	var buf strings.Builder

	buf.WriteString("set(FROTH_PROJECT_FFI_SOURCES\n")
	for _, s := range resolved.Sources {
		fmt.Fprintf(&buf, "    %q\n", s)
	}
	buf.WriteString(")\n")

	buf.WriteString("set(FROTH_PROJECT_FFI_INCLUDES\n")
	for _, inc := range resolved.Includes {
		fmt.Fprintf(&buf, "    %q\n", inc)
	}
	buf.WriteString(")\n")

	buf.WriteString("set(FROTH_PROJECT_FFI_DEFINES\n")
	for _, def := range resolved.Defines {
		fmt.Fprintf(&buf, "    %q\n", def)
	}
	buf.WriteString(")\n")

	configPath := filepath.Join(buildDir, "project_ffi.cmake")
	if err := os.WriteFile(configPath, []byte(buf.String()), 0644); err != nil {
		return "", fmt.Errorf("write project ffi config: %w", err)
	}

	absPath, err := filepath.Abs(configPath)
	if err != nil {
		return "", fmt.Errorf("abs project ffi config: %w", err)
	}

	return absPath, nil
}

func buildPosixManifest(manifest *project.Manifest, root string, runtimeSourcePath string, ffiConfigPath string) error {
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return fmt.Errorf("kernel source not found: %w", err)
	}

	firmwareDir := filepath.Join(root, ".froth-build", "firmware")
	if err := os.MkdirAll(firmwareDir, 0755); err != nil {
		return fmt.Errorf("create firmware dir: %w", err)
	}

	cmakeArgs := []string{
		kernelRoot,
		fmt.Sprintf("-DFROTH_BOARD=%s", manifest.Target.Board),
		fmt.Sprintf("-DFROTH_PLATFORM=%s", manifest.Target.Platform),
		"-DFROTHY_BUILD_HOST=ON",
	}
	cmakeArgs = append(cmakeArgs, manifest.Build.CMakeArgs()...)
	if ffiConfigPath != "" {
		cmakeArgs = append(cmakeArgs, fmt.Sprintf("-DFROTH_PROJECT_FFI_CONFIG=%s", ffiConfigPath))
	}

	cmake := exec.Command("cmake", cmakeArgs...)
	cmake.Dir = firmwareDir
	cmake.Stdout = os.Stdout
	cmake.Stderr = os.Stderr
	if err := cmake.Run(); err != nil {
		return fmt.Errorf("cmake: %w", err)
	}

	makePath, _, err := findMakeTool(buildLookPath)
	if err != nil {
		return fmt.Errorf("make: %w", err)
	}

	mk := exec.Command(makePath)
	mk.Dir = firmwareDir
	mk.Stdout = os.Stdout
	mk.Stderr = os.Stderr
	if err := mk.Run(); err != nil {
		return fmt.Errorf("make: %w", err)
	}

	if err := seedBuiltImage(filepath.Join(firmwareDir, "Frothy"), root, runtimeSourcePath); err != nil {
		return err
	}

	fmt.Printf("\nFirmware ready: %s\n", filepath.Join(firmwareDir, "Frothy"))
	return nil
}

func buildESPIDFManifest(manifest *project.Manifest, root string, runtimeSourcePath string, ffiConfigPath string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	kernelRoot, err := findKernelRoot()
	if err != nil {
		return fmt.Errorf("kernel source not found: %w", err)
	}

	targetDir, err := stageESPIDFTarget(kernelRoot, root)
	if err != nil {
		return err
	}

	// Validate board/platform are simple identifiers before passing them to CMake.
	if !isShellSafe(manifest.Target.Board) {
		return fmt.Errorf("invalid board name: %s", manifest.Target.Board)
	}
	if !isShellSafe(manifest.Target.Platform) {
		return fmt.Errorf("invalid platform name: %s", manifest.Target.Platform)
	}

	args := []string{
		"-c",
		`. "$IDF_EXPORT" && exec idf.py "$@"`,
		"bash",
		fmt.Sprintf("-DFROTH_BOARD=%s", manifest.Target.Board),
		fmt.Sprintf("-DFROTH_PLATFORM=%s", manifest.Target.Platform),
	}
	args = append(args, manifest.Build.CMakeArgs()...)
	if ffiConfigPath != "" {
		args = append(args, fmt.Sprintf("-DFROTH_PROJECT_FFI_CONFIG=%s", ffiConfigPath))
	}
	args = append(args, "build")

	cmd := exec.Command("bash", args...)
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(),
		"IDF_EXPORT="+exportPath,
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py build: %w", err)
	}

	fmt.Fprintf(os.Stderr, "note: esp-idf builds no longer bake project source into firmware; runtime source saved at %s\n", runtimeSourcePath)
	return nil
}

func stageESPIDFTarget(kernelRoot string, projectRoot string) (string, error) {
	sourceDir := filepath.Join(kernelRoot, "targets", "esp-idf")
	if _, err := os.Stat(sourceDir); err != nil {
		return "", fmt.Errorf("esp-idf target dir not found: %s", sourceDir)
	}

	stagedDir := filepath.Join(projectRoot, ".froth-build", "esp-idf")
	stagedMainCMake := filepath.Join(stagedDir, "main", "CMakeLists.txt")
	if _, err := os.Stat(stagedMainCMake); err != nil {
		if !os.IsNotExist(err) {
			return "", fmt.Errorf("check staged esp-idf target: %w", err)
		}
		if err := copyESPIDFScaffold(sourceDir, stagedDir); err != nil {
			return "", fmt.Errorf("stage esp-idf target: %w", err)
		}
	} else {
		// Always refresh main/CMakeLists.txt from the current template
		// so that template changes (e.g. project FFI support) propagate
		// into existing staged directories without requiring --clean.
		sourceMainCMake := filepath.Join(sourceDir, "main", "CMakeLists.txt")
		data, err := os.ReadFile(sourceMainCMake)
		if err != nil {
			return "", fmt.Errorf("read esp-idf template: %w", err)
		}
		if err := os.WriteFile(stagedMainCMake, data, 0644); err != nil {
			return "", fmt.Errorf("refresh staged esp-idf cmake: %w", err)
		}
	}
	if err := patchESPIDFMainCMake(stagedDir, kernelRoot); err != nil {
		return "", fmt.Errorf("patch staged esp-idf cmake: %w", err)
	}
	return stagedDir, nil
}

func copyESPIDFScaffold(sourceDir string, destDir string) error {
	if err := os.MkdirAll(destDir, 0755); err != nil {
		return fmt.Errorf("create staged target dir: %w", err)
	}

	return filepath.WalkDir(sourceDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}

		rel, err := filepath.Rel(sourceDir, path)
		if err != nil {
			return err
		}
		if rel == "." {
			return nil
		}

		if shouldSkipESPIDFScaffoldPath(rel) {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}

		destPath := filepath.Join(destDir, rel)
		if d.IsDir() {
			info, err := d.Info()
			if err != nil {
				return err
			}
			mode := info.Mode().Perm()
			if mode == 0 {
				mode = 0755
			}
			return os.MkdirAll(destPath, mode)
		}

		info, err := d.Info()
		if err != nil {
			return err
		}
		if !info.Mode().IsRegular() {
			return nil
		}

		data, err := os.ReadFile(path)
		if err != nil {
			return fmt.Errorf("read %s: %w", path, err)
		}
		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return err
		}
		if err := os.WriteFile(destPath, data, info.Mode().Perm()); err != nil {
			return fmt.Errorf("write %s: %w", destPath, err)
		}
		return nil
	})
}

func shouldSkipESPIDFScaffoldPath(rel string) bool {
	parts := strings.Split(filepath.ToSlash(rel), "/")
	if len(parts) == 0 {
		return false
	}

	switch parts[0] {
	case ".cache", "build", "managed_components":
		return true
	}

	if len(parts) == 1 {
		switch parts[0] {
		case "sdkconfig", "sdkconfig.old", "dependencies.lock":
			return true
		}
	}

	return false
}

func patchESPIDFMainCMake(stagedDir string, kernelRoot string) error {
	mainCMakePath := filepath.Join(stagedDir, "main", "CMakeLists.txt")
	data, err := os.ReadFile(mainCMakePath)
	if err != nil {
		return err
	}

	absKernelRoot, err := filepath.Abs(kernelRoot)
	if err != nil {
		return fmt.Errorf("abs kernel root: %w", err)
	}
	replacement := fmt.Sprintf("set(FROTH_ROOT %q)", filepath.ToSlash(absKernelRoot))

	original := `set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")`
	previousPatched := `if(DEFINED FROTH_KERNEL_ROOT)
    set(FROTH_ROOT "${FROTH_KERNEL_ROOT}")
else()
    set(FROTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
endif()`

	content := string(data)
	if strings.Contains(content, replacement) {
		return nil
	}
	switch {
	case strings.Contains(content, previousPatched):
		content = strings.Replace(content, previousPatched, replacement, 1)
	case strings.Contains(content, original):
		content = strings.Replace(content, original, replacement, 1)
	default:
		return fmt.Errorf("expected FROTH_ROOT definition not found in %s", mainCMakePath)
	}

	return os.WriteFile(mainCMakePath, []byte(content), 0644)
}

func seedFrothyImage(binaryPath string, runDir string, runtimeSourcePath string) error {
	sourceBytes, err := os.ReadFile(runtimeSourcePath)
	if err != nil {
		return fmt.Errorf("read runtime source: %w", err)
	}

	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		LocalRuntimePath: binaryPath,
		LocalRuntimeDir:  runDir,
	})
	if _, err := manager.Connect(""); err != nil {
		return fmt.Errorf("start local Frothy runtime: %w", err)
	}
	defer manager.Disconnect()

	if err := seedFrothyRuntime(manager, string(sourceBytes)); err != nil {
		return fmt.Errorf("seed source: %w", err)
	}
	return nil
}

func seedFrothyRuntime(manager *frothycontrol.Manager, source string) error {
	if _, err := runControlSource(manager, source); err != nil {
		return err
	}

	words, err := manager.Words()
	if err != nil {
		return fmt.Errorf("seed words: %w", err)
	}
	if contains(words, "autorun") && !contains(words, "boot") {
		if _, err := runControlEval(manager, "boot is fn [ autorun: ]"); err != nil {
			return fmt.Errorf("seed boot alias: %w", err)
		}
	}

	if _, err := manager.Save(func(data []byte) {
		_, _ = os.Stdout.Write(data)
	}); err != nil {
		return fmt.Errorf("save: %w", err)
	}
	return nil
}

// runBuildLegacy is the old build path for when there's no froth.toml
// (building the kernel repo directly).
func runBuildLegacy() error {
	root, err := findLocalKernelRoot()
	if err != nil {
		return err
	}

	switch targetFlag {
	case "", "posix":
		if err := cleanBuildDirIfRequested(filepath.Join(root, "build")); err != nil {
			return err
		}
		return buildPosix(root)
	case "esp-idf":
		if err := cleanBuildDirIfRequested(filepath.Join(root, "targets", "esp-idf", "build")); err != nil {
			return err
		}
		return buildESPIDF(root)
	default:
		return fmt.Errorf("unknown target: %s", targetFlag)
	}
}

func cleanBuildDirIfRequested(dir string) error {
	if !cleanFlag {
		return nil
	}

	if _, err := os.Stat(dir); err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return fmt.Errorf("check build dir: %w", err)
	}

	if err := os.RemoveAll(dir); err != nil {
		return fmt.Errorf("remove build dir: %w", err)
	}

	fmt.Println("Cleaned build directory")
	return nil
}

func buildPosix(root string) error {
	buildDir := filepath.Join(root, "build")
	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return fmt.Errorf("create build dir: %w", err)
	}

	cmake := exec.Command("cmake", "..", "-U", "FROTH_*", "-DFROTH_CELL_SIZE_BITS=32", "-DFROTHY_BUILD_HOST=ON")
	cmake.Dir = buildDir
	cmake.Stdout = os.Stdout
	cmake.Stderr = os.Stderr
	if err := cmake.Run(); err != nil {
		return fmt.Errorf("cmake: %w", err)
	}

	makePath, _, err := findMakeTool(buildLookPath)
	if err != nil {
		return fmt.Errorf("make: %w", err)
	}

	mk := exec.Command(makePath)
	mk.Dir = buildDir
	mk.Stdout = os.Stdout
	mk.Stderr = os.Stderr
	if err := mk.Run(); err != nil {
		return fmt.Errorf("make: %w", err)
	}

	fmt.Printf("\nbinary: %s\n", filepath.Join(buildDir, "Frothy"))
	return nil
}

func buildESPIDF(root string) error {
	exportPath, err := espIDFExportPath()
	if err != nil {
		return err
	}

	targetDir := filepath.Join(root, "targets", "esp-idf")
	if _, err := os.Stat(targetDir); err != nil {
		return fmt.Errorf("target dir not found: %s", targetDir)
	}

	cmd := exec.Command("bash", "-c", ". \"$IDF_EXPORT\" && idf.py build")
	cmd.Dir = targetDir
	cmd.Env = append(os.Environ(), "IDF_EXPORT="+exportPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("idf.py build: %w", err)
	}

	return nil
}

func findLocalKernelRoot() (string, error) {
	startDir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}

	if root, err := findExecutableKernelRoot(); err == nil {
		return root, nil
	}

	return findLocalKernelRootFrom(startDir)
}

func findLocalKernelRootFrom(startDir string) (string, error) {
	dir := startDir
	for {
		cmake := filepath.Join(dir, "CMakeLists.txt")
		vm := filepath.Join(dir, "src", "froth_vm.h")
		if _, err := os.Stat(cmake); err == nil {
			if _, err := os.Stat(vm); err == nil {
				return dir, nil
			}
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("kernel source not found (no CMakeLists.txt + src/froth_vm.h)")
		}
		dir = parent
	}
}

func findExecutableKernelRoot() (string, error) {
	exePath, err := os.Executable()
	if err != nil {
		return "", err
	}
	launchName := filepath.Base(exePath)

	resolvedPath, err := filepath.EvalSymlinks(exePath)
	if err == nil {
		exePath = resolvedPath
	}

	if launchName != "frothy" && filepath.Base(exePath) != "frothy" {
		return "", fmt.Errorf("executable name is not frothy")
	}

	cliDir := filepath.Dir(exePath)
	root := filepath.Clean(filepath.Join(cliDir, "..", ".."))
	cmake := filepath.Join(root, "CMakeLists.txt")
	vm := filepath.Join(root, "src", "froth_vm.h")
	if _, err := os.Stat(cmake); err != nil {
		return "", fmt.Errorf("kernel source not found beside executable")
	}
	if _, err := os.Stat(vm); err != nil {
		return "", fmt.Errorf("kernel source not found beside executable")
	}

	return root, nil
}

// findKernelRoot walks up from CWD looking for a local kernel checkout first,
// then falls back to the extracted SDK cache.
func findKernelRoot() (string, error) {
	startDir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}
	return findKernelRootFrom(startDir)
}

func findKernelRootFrom(startDir string) (string, error) {
	if root, err := findLocalKernelRootFrom(startDir); err == nil {
		return root, nil
	}

	sdkRoot, err := ensureSDKRoot()
	if err != nil {
		return "", fmt.Errorf("kernel source not found locally and sdk extraction failed: %w", err)
	}
	return sdkRoot, nil
}

var shellSafePattern = regexp.MustCompile(`^[a-zA-Z0-9_-]+$`)

func isShellSafe(s string) bool {
	return shellSafePattern.MatchString(s)
}

func espIDFExportPath() (string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", err
	}
	p := filepath.Join(home, "sdk", "esp-idf", "export.sh")
	if _, err := os.Stat(p); err != nil {
		return "", fmt.Errorf("ESP-IDF not found (run `froth setup esp-idf`)")
	}
	return p, nil
}
