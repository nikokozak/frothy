package cmd

import (
	"archive/zip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/project"
	"github.com/nikokozak/froth/tools/cli/internal/sdk"
	serialpkg "github.com/nikokozak/froth/tools/cli/internal/serial"
)

const prebuiltFirmwareBoard = "esp32-devkit-v1"

var flashLookPath = exec.LookPath
var flashBuildManifest = runBuildManifest
var flashResolvePort = resolveFlashPort
var flashESPIDFDirFn = flashESPIDFDir
var flashApplyRuntime = applyRuntimeAfterFlash

type firmwareManifest struct {
	WriteFlashArgs   []string          `json:"write_flash_args"`
	FlashFiles       map[string]string `json:"flash_files"`
	ExtraEsptoolArgs struct {
		After  string `json:"after"`
		Before string `json:"before"`
		Stub   *bool  `json:"stub"`
		Chip   string `json:"chip"`
	} `json:"extra_esptool_args"`
}

type flashFile struct {
	Offset string
	Path   string
	Value  uint64
}

func runFlash() error {
	cwd, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err == nil {
		if targetFlag != "" {
			return fmt.Errorf("`froth flash` does not accept --target inside a project; edit [target] in froth.toml")
		}
		return runFlashManifest(manifest, root)
	}
	if _, rootErr := project.FindProjectRoot(cwd); rootErr == nil {
		return err
	}

	if _, localErr := findLocalKernelRoot(); localErr == nil {
		return runFlashLegacy()
	}

	return runFlashPrebuilt()
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

	switch targetFlag {
	case "", "posix":
		fmt.Println("no flash step for POSIX target")
		fmt.Printf("binary: %s\n", filepath.Join(root, "build", "Frothy"))
		return nil
	case "esp-idf":
		return flashESPIDF(root)
	default:
		return fmt.Errorf("unknown target: %s", targetFlag)
	}
}

func runFlashPrebuilt() error {
	if targetFlag != "" && targetFlag != "esp-idf" {
		return fmt.Errorf("pre-built flashing only supports the default ESP32 firmware; remove --target %s or create a project with `froth new`", targetFlag)
	}

	version, err := cliVersion()
	if err != nil {
		return err
	}

	cacheDir, err := firmwareCacheDir(version)
	if err != nil {
		return err
	}

	manifest, err := validateFirmwareDir(cacheDir)
	if err != nil {
		manifest, err = populateFirmwareCache(version, cacheDir)
		if err != nil {
			return fmt.Errorf("pre-built firmware not available for v%s: %w\nTo build from source, create a project with `froth new` and run `froth build && froth flash`.", version, err)
		}
	}

	port, err := resolveFlashPort()
	if err != nil {
		return err
	}

	fmt.Printf("Flashing pre-built firmware v%s to %s...\n", version, port)
	return flashPrebuiltFirmware(cacheDir, manifest, port)
}

func firmwareCacheDir(version string) (string, error) {
	home, err := sdk.FrothHome()
	if err != nil {
		return "", err
	}

	return filepath.Join(home, "firmware", "v"+version, prebuiltFirmwareBoard), nil
}

func populateFirmwareCache(version string, cacheDir string) (*firmwareManifest, error) {
	parentDir := filepath.Dir(cacheDir)
	if err := os.MkdirAll(parentDir, 0755); err != nil {
		return nil, fmt.Errorf("create firmware cache parent: %w", err)
	}

	tempDir, err := os.MkdirTemp(parentDir, ".firmware-*")
	if err != nil {
		return nil, fmt.Errorf("create firmware temp dir: %w", err)
	}
	defer os.RemoveAll(tempDir)

	archivePath := filepath.Join(tempDir, firmwareZipAssetName(version))
	if err := downloadReleaseAsset(version, firmwareZipAssetName(version), archivePath); err != nil {
		return nil, err
	}

	checksums, err := fetchReleaseChecksums(version)
	if err != nil {
		return nil, err
	}

	wantSHA, ok := checksums[firmwareZipAssetName(version)]
	if !ok {
		return nil, fmt.Errorf("checksums missing %s", firmwareZipAssetName(version))
	}
	if err := verifyFileSHA256(archivePath, wantSHA); err != nil {
		return nil, err
	}

	extractDir := filepath.Join(tempDir, "extract")
	if err := extractFirmwareZip(archivePath, extractDir); err != nil {
		return nil, err
	}

	manifest, err := validateFirmwareDir(extractDir)
	if err != nil {
		return nil, err
	}

	if err := activateDirectory(cacheDir, extractDir); err != nil {
		return nil, fmt.Errorf("activate firmware cache: %w", err)
	}

	return manifest, nil
}

func downloadReleaseAsset(version string, asset string, destPath string) error {
	return downloadFile(releaseAssetURL(version, asset), destPath)
}

func fetchReleaseChecksums(version string) (map[string]string, error) {
	url := releaseAssetURL(version, checksumsAssetName(version))
	resp, err := setupHTTPClient.Get(url)
	if err != nil {
		return nil, fmt.Errorf("GET %s: %w", url, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %s: unexpected status %s", url, resp.Status)
	}

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read checksums: %w", err)
	}

	checksums := make(map[string]string)
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		fields := strings.Fields(line)
		if len(fields) < 2 {
			return nil, fmt.Errorf("invalid checksum line: %s", line)
		}
		checksums[filepath.Base(fields[len(fields)-1])] = fields[0]
	}

	return checksums, nil
}

func verifyFileSHA256(path string, want string) error {
	file, err := os.Open(path)
	if err != nil {
		return fmt.Errorf("open %s: %w", path, err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return fmt.Errorf("hash %s: %w", path, err)
	}

	got := hex.EncodeToString(hash.Sum(nil))
	if !strings.EqualFold(got, want) {
		return fmt.Errorf("checksum mismatch for %s: got %s want %s", filepath.Base(path), got, want)
	}

	return nil
}

func extractFirmwareZip(archivePath string, destDir string) error {
	reader, err := zip.OpenReader(archivePath)
	if err != nil {
		return fmt.Errorf("open %s: %w", archivePath, err)
	}
	defer reader.Close()

	if err := os.MkdirAll(destDir, 0755); err != nil {
		return fmt.Errorf("create extract dir: %w", err)
	}

	for _, file := range reader.File {
		destPath, err := safeJoin(destDir, file.Name)
		if err != nil {
			return err
		}

		info := file.FileInfo()
		if info.IsDir() {
			if err := os.MkdirAll(destPath, 0755); err != nil {
				return fmt.Errorf("create %s: %w", destPath, err)
			}
			continue
		}
		if info.Mode()&os.ModeSymlink != 0 {
			return fmt.Errorf("zip contains unsupported symlink: %s", file.Name)
		}
		if !info.Mode().IsRegular() {
			return fmt.Errorf("zip contains unsupported entry: %s", file.Name)
		}

		if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
			return fmt.Errorf("create dir for %s: %w", destPath, err)
		}

		src, err := file.Open()
		if err != nil {
			return fmt.Errorf("open zip entry %s: %w", file.Name, err)
		}

		dst, err := os.OpenFile(destPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
		if err != nil {
			src.Close()
			return fmt.Errorf("create %s: %w", destPath, err)
		}

		_, copyErr := io.Copy(dst, src)
		closeErr := dst.Close()
		srcErr := src.Close()
		if copyErr != nil {
			return fmt.Errorf("write %s: %w", destPath, copyErr)
		}
		if closeErr != nil {
			return fmt.Errorf("close %s: %w", destPath, closeErr)
		}
		if srcErr != nil {
			return fmt.Errorf("close zip entry %s: %w", file.Name, srcErr)
		}
	}

	return nil
}

func validateFirmwareDir(dir string) (*firmwareManifest, error) {
	manifestPath := filepath.Join(dir, "flasher_args.json")
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return nil, fmt.Errorf("firmware cache invalid: read flasher_args.json: %w", err)
	}

	var manifest firmwareManifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		return nil, fmt.Errorf("firmware cache invalid: parse flasher_args.json: %w", err)
	}
	if manifest.ExtraEsptoolArgs.Chip == "" {
		return nil, fmt.Errorf("firmware cache invalid: missing chip in flasher_args.json")
	}
	if len(manifest.FlashFiles) == 0 {
		return nil, fmt.Errorf("firmware cache invalid: missing flash_files in flasher_args.json")
	}

	for _, relPath := range manifest.FlashFiles {
		fullPath, err := safeJoin(dir, relPath)
		if err != nil {
			return nil, err
		}
		info, err := os.Stat(fullPath)
		if err != nil {
			return nil, fmt.Errorf("firmware cache invalid: missing %s: %w", relPath, err)
		}
		if !info.Mode().IsRegular() {
			return nil, fmt.Errorf("firmware cache invalid: %s is not a regular file", relPath)
		}
	}

	return &manifest, nil
}

func flashPrebuiltFirmware(root string, manifest *firmwareManifest, port string) error {
	esptoolPath, err := resolveEsptool()
	if err != nil {
		return err
	}

	files, err := orderedFlashFiles(manifest)
	if err != nil {
		return err
	}

	args := []string{
		"--chip", manifest.ExtraEsptoolArgs.Chip,
		"--port", port,
	}
	if manifest.ExtraEsptoolArgs.Before != "" {
		args = append(args, "--before", manifest.ExtraEsptoolArgs.Before)
	}
	if manifest.ExtraEsptoolArgs.After != "" {
		args = append(args, "--after", manifest.ExtraEsptoolArgs.After)
	}
	if manifest.ExtraEsptoolArgs.Stub != nil && !*manifest.ExtraEsptoolArgs.Stub {
		args = append(args, "--no-stub")
	}

	args = append(args, "write_flash")
	args = append(args, manifest.WriteFlashArgs...)
	for _, file := range files {
		fullPath, err := safeJoin(root, file.Path)
		if err != nil {
			return err
		}
		args = append(args, file.Offset, fullPath)
	}

	cmd := exec.Command(esptoolPath, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("esptool flash: %w", err)
	}

	return nil
}

func orderedFlashFiles(manifest *firmwareManifest) ([]flashFile, error) {
	files := make([]flashFile, 0, len(manifest.FlashFiles))
	for offset, path := range manifest.FlashFiles {
		value, err := strconv.ParseUint(offset, 0, 64)
		if err != nil {
			return nil, fmt.Errorf("invalid flash offset %s: %w", offset, err)
		}
		files = append(files, flashFile{Offset: offset, Path: path, Value: value})
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].Value < files[j].Value
	})

	return files, nil
}

func resolveEsptool() (string, error) {
	for _, candidate := range []string{"esptool.py", "esptool"} {
		path, err := flashLookPath(candidate)
		if err == nil {
			return path, nil
		}
	}

	return "", fmt.Errorf("esptool is required for flashing. Install it with:\n  brew install esptool\n  pip install esptool")
}

func firmwareZipAssetName(version string) string {
	return fmt.Sprintf("frothy-v%s-%s.zip", version, prebuiltFirmwareBoard)
}

func checksumsAssetName(version string) string {
	return fmt.Sprintf("frothy-v%s-checksums.txt", version)
}

func activateDirectory(targetDir string, sourceDir string) error {
	parentDir := filepath.Dir(targetDir)
	backupDir := ""

	if _, err := os.Stat(targetDir); err == nil {
		backupDir = filepath.Join(parentDir, "."+filepath.Base(targetDir)+"-backup")
		if err := os.RemoveAll(backupDir); err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("clear firmware backup dir: %w", err)
		}
		if err := os.Rename(targetDir, backupDir); err != nil {
			return fmt.Errorf("stage existing firmware cache: %w", err)
		}
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("stat existing firmware cache: %w", err)
	}

	if err := os.Rename(sourceDir, targetDir); err != nil {
		if backupDir != "" {
			_ = os.Rename(backupDir, targetDir)
		}
		return err
	}

	if backupDir != "" {
		if err := os.RemoveAll(backupDir); err != nil {
			return fmt.Errorf("remove old firmware cache backup: %w", err)
		}
	}

	return nil
}

func safeJoin(root string, rel string) (string, error) {
	if filepath.IsAbs(rel) {
		return "", fmt.Errorf("path escapes firmware root: %s", rel)
	}

	cleanRel := filepath.Clean(rel)
	fullPath := filepath.Join(root, cleanRel)
	absRoot, err := filepath.Abs(root)
	if err != nil {
		return "", fmt.Errorf("abs root: %w", err)
	}
	absPath, err := filepath.Abs(fullPath)
	if err != nil {
		return "", fmt.Errorf("abs path: %w", err)
	}
	if absPath != absRoot && !strings.HasPrefix(absPath, absRoot+string(filepath.Separator)) {
		return "", fmt.Errorf("path escapes firmware root: %s", rel)
	}

	return absPath, nil
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
