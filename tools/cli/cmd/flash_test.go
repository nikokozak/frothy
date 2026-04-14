package cmd

import (
	"archive/zip"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/nikokozak/froth/tools/cli/internal/project"
)

func TestRunFlashManifestReportsFrothyPosixBinary(t *testing.T) {
	resetCommandGlobals(t)

	root := t.TempDir()
	manifest := &project.Manifest{
		Target: project.TargetConfig{
			Board:    "posix",
			Platform: "posix",
		},
	}

	oldBuildManifest := flashBuildManifest
	flashBuildManifest = func(m *project.Manifest, gotRoot string) error {
		if gotRoot != root {
			t.Fatalf("build root = %q, want %q", gotRoot, root)
		}
		return nil
	}
	t.Cleanup(func() { flashBuildManifest = oldBuildManifest })

	stdout, _ := captureOutput(t, func() {
		if err := runFlashManifest(manifest, root); err != nil {
			t.Fatalf("runFlashManifest: %v", err)
		}
	})

	if !strings.Contains(stdout, filepath.Join(root, ".froth-build", "firmware", "Frothy")) {
		t.Fatalf("stdout = %q, want Frothy binary path", stdout)
	}
}

func TestRunFlashManifestAppliesRuntimeImageAfterESPIDFFlash(t *testing.T) {
	resetCommandGlobals(t)

	root := t.TempDir()
	manifest := &project.Manifest{
		Target: project.TargetConfig{
			Board:    "esp32-devkit-v1",
			Platform: "esp-idf",
		},
	}

	oldBuildManifest := flashBuildManifest
	oldResolvePort := flashResolvePort
	oldFlashDir := flashESPIDFDirFn
	oldApplyRuntime := flashApplyRuntime
	flashBuildManifest = func(*project.Manifest, string) error { return nil }
	flashResolvePort = func() (string, error) { return "/dev/cu.mock", nil }

	var flashedDir, flashedPort string
	flashESPIDFDirFn = func(dir string, port string) error {
		flashedDir = dir
		flashedPort = port
		return nil
	}

	var appliedPort, appliedRuntime string
	flashApplyRuntime = func(port string, runtimePath string) error {
		appliedPort = port
		appliedRuntime = runtimePath
		return nil
	}

	t.Cleanup(func() {
		flashBuildManifest = oldBuildManifest
		flashResolvePort = oldResolvePort
		flashESPIDFDirFn = oldFlashDir
		flashApplyRuntime = oldApplyRuntime
	})

	stdout, _ := captureOutput(t, func() {
		if err := runFlashManifest(manifest, root); err != nil {
			t.Fatalf("runFlashManifest: %v", err)
		}
	})

	if flashedDir != filepath.Join(root, ".froth-build", "esp-idf") {
		t.Fatalf("flash dir = %q", flashedDir)
	}
	if flashedPort != "/dev/cu.mock" {
		t.Fatalf("flash port = %q", flashedPort)
	}
	if appliedPort != "/dev/cu.mock" {
		t.Fatalf("apply port = %q", appliedPort)
	}
	if appliedRuntime != filepath.Join(root, ".froth-build", "runtime.frothy") {
		t.Fatalf("runtime path = %q", appliedRuntime)
	}
	if !strings.Contains(stdout, "Applying runtime source: "+filepath.Join(root, ".froth-build", "runtime.frothy")) {
		t.Fatalf("stdout = %q, want runtime apply message", stdout)
	}
}

func TestRunFlashPrebuiltUsesValidCache(t *testing.T) {
	resetCommandGlobals(t)

	home := filepath.Join(t.TempDir(), "froth-home")
	t.Setenv("FROTH_HOME", home)
	version := frothVersion(t)
	withChdir(t, t.TempDir())
	portFlag = "/dev/cu.usbserial-test"

	cacheDir, err := firmwareCacheDir(version)
	if err != nil {
		t.Fatalf("firmwareCacheDir: %v", err)
	}
	writeFirmwareFixture(t, cacheDir)

	logPath := filepath.Join(t.TempDir(), "esptool.log")
	esptoolPath := filepath.Join(t.TempDir(), "esptool.py")
	mustWriteExecutable(t, esptoolPath, "#!/bin/sh\nprintf '%s\\n' \"$*\" > \""+logPath+"\"\n")

	oldFlashLookPath := flashLookPath
	flashLookPath = func(name string) (string, error) {
		if name == "esptool.py" {
			return esptoolPath, nil
		}
		return "", os.ErrNotExist
	}
	t.Cleanup(func() { flashLookPath = oldFlashLookPath })

	if err := runFlash(); err != nil {
		t.Fatalf("runFlash: %v", err)
	}

	log := mustReadFile(t, logPath)
	for _, want := range []string{
		"--chip esp32",
		"--port /dev/cu.usbserial-test",
		"--before default_reset",
		"--after hard_reset",
		"write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m",
		filepath.Join(cacheDir, "bootloader", "bootloader.bin"),
		filepath.Join(cacheDir, "partition_table", "partition-table.bin"),
		filepath.Join(cacheDir, "froth.bin"),
	} {
		if !strings.Contains(log, want) {
			t.Fatalf("esptool log = %q, want %q", log, want)
		}
	}
	if strings.Index(log, "0x1000") > strings.Index(log, "0x8000") || strings.Index(log, "0x8000") > strings.Index(log, "0x10000") {
		t.Fatalf("esptool log = %q, want sorted flash offsets", log)
	}
}

func TestRunFlashPrebuiltReDownloadsWhenCacheIsInvalid(t *testing.T) {
	resetCommandGlobals(t)

	home := filepath.Join(t.TempDir(), "froth-home")
	t.Setenv("FROTH_HOME", home)
	version := frothVersion(t)
	withChdir(t, t.TempDir())
	portFlag = "/dev/cu.usbserial-test"

	cacheDir, err := firmwareCacheDir(version)
	if err != nil {
		t.Fatalf("firmwareCacheDir: %v", err)
	}
	writeFirmwareFixture(t, cacheDir)
	if err := os.Remove(filepath.Join(cacheDir, "bootloader", "bootloader.bin")); err != nil {
		t.Fatalf("remove cached file: %v", err)
	}

	archiveBytes := firmwareZipFixture(t)
	checksums := fmt.Sprintf("%s  dist/%s\n", sha256Hex(archiveBytes), firmwareZipAssetName(version))
	server := releaseServer(t, version, archiveBytes, checksums)
	defer server.Close()

	oldReleaseBase := releaseDownloadBase
	releaseDownloadBase = server.URL + "/download"
	t.Cleanup(func() { releaseDownloadBase = oldReleaseBase })

	logPath := filepath.Join(t.TempDir(), "esptool.log")
	esptoolPath := filepath.Join(t.TempDir(), "esptool.py")
	mustWriteExecutable(t, esptoolPath, "#!/bin/sh\nprintf '%s\\n' \"$*\" > \""+logPath+"\"\n")

	oldFlashLookPath := flashLookPath
	flashLookPath = func(name string) (string, error) {
		if name == "esptool.py" {
			return esptoolPath, nil
		}
		return "", os.ErrNotExist
	}
	t.Cleanup(func() { flashLookPath = oldFlashLookPath })

	if err := runFlash(); err != nil {
		t.Fatalf("runFlash: %v", err)
	}

	if _, err := os.Stat(filepath.Join(cacheDir, "bootloader", "bootloader.bin")); err != nil {
		t.Fatalf("cached bootloader missing after re-download: %v", err)
	}
}

func TestRunFlashPrebuiltRejectsChecksumMismatch(t *testing.T) {
	resetCommandGlobals(t)

	home := filepath.Join(t.TempDir(), "froth-home")
	t.Setenv("FROTH_HOME", home)
	version := frothVersion(t)
	withChdir(t, t.TempDir())
	portFlag = "/dev/cu.usbserial-test"

	archiveBytes := firmwareZipFixture(t)
	checksums := fmt.Sprintf("%s  %s\n", strings.Repeat("0", 64), firmwareZipAssetName(version))
	server := releaseServer(t, version, archiveBytes, checksums)
	defer server.Close()

	oldReleaseBase := releaseDownloadBase
	releaseDownloadBase = server.URL + "/download"
	t.Cleanup(func() { releaseDownloadBase = oldReleaseBase })

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "checksum mismatch") {
		t.Fatalf("error = %v, want checksum mismatch", err)
	}
}

func TestRunFlashPrebuiltRequiresEsptool(t *testing.T) {
	resetCommandGlobals(t)

	home := filepath.Join(t.TempDir(), "froth-home")
	t.Setenv("FROTH_HOME", home)
	version := frothVersion(t)
	withChdir(t, t.TempDir())
	portFlag = "/dev/cu.usbserial-test"

	cacheDir, err := firmwareCacheDir(version)
	if err != nil {
		t.Fatalf("firmwareCacheDir: %v", err)
	}
	writeFirmwareFixture(t, cacheDir)

	oldFlashLookPath := flashLookPath
	flashLookPath = func(string) (string, error) { return "", os.ErrNotExist }
	t.Cleanup(func() { flashLookPath = oldFlashLookPath })

	err = runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "brew install esptool") {
		t.Fatalf("error = %v, want esptool install instructions", err)
	}
}

func TestRunFlashPrefersLocalCheckout(t *testing.T) {
	resetCommandGlobals(t)

	repoRoot := t.TempDir()
	mustWriteFile(t, filepath.Join(repoRoot, "CMakeLists.txt"), "project(Froth)\n")
	mustWriteFile(t, filepath.Join(repoRoot, "src", "froth_vm.h"), "/* vm */\n")
	mustWriteFile(t, filepath.Join(repoRoot, "targets", "esp-idf", "CMakeLists.txt"), "project(froth)\n")
	if err := os.MkdirAll(filepath.Join(repoRoot, "nested"), 0755); err != nil {
		t.Fatalf("mkdir nested: %v", err)
	}
	withChdir(t, filepath.Join(repoRoot, "nested"))
	targetFlag = "esp-idf"
	portFlag = "/dev/cu.usbserial-test"
	t.Setenv("FROTH_HOME", filepath.Join(t.TempDir(), "froth-home"))

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "ESP-IDF not found") {
		t.Fatalf("error = %v, want local checkout flash path", err)
	}
}

func TestRunFlashRejectsLegacyTargetFlagInsideManifestProject(t *testing.T) {
	resetCommandGlobals(t)
	projectRoot := t.TempDir()
	targetFlag = "esp-idf"
	withChdir(t, projectRoot)
	writeManifestProject(t, projectRoot, `[project]
name = "demo"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"
`)
	mustWriteFile(t, filepath.Join(projectRoot, "src", "main.froth"), "boot { nil }\n")

	err := runFlash()
	if err == nil {
		t.Fatal("runFlash succeeded, want error")
	}
	if !strings.Contains(err.Error(), "does not accept --target inside a project") {
		t.Fatalf("runFlash error = %v, want manifest target guidance", err)
	}
}

func TestValidateFirmwareDirRejectsEscapingPath(t *testing.T) {
	resetCommandGlobals(t)

	dir := t.TempDir()
	mustWriteFile(t, filepath.Join(dir, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {"0x1000": "../evil.bin"},
  "extra_esptool_args": {"chip": "esp32"}
}`)

	_, err := validateFirmwareDir(dir)
	if err == nil {
		t.Fatal("validateFirmwareDir succeeded, want error")
	}
	if !strings.Contains(err.Error(), "path escapes firmware root") {
		t.Fatalf("error = %v, want path escape rejection", err)
	}
}

func writeFirmwareFixture(t *testing.T, root string) {
	t.Helper()

	mustWriteFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio", "--flash_size", "4MB", "--flash_freq", "40m"],
  "flash_files": {
    "0x10000": "froth.bin",
    "0x1000": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin"
  },
  "extra_esptool_args": {
    "before": "default_reset",
    "after": "hard_reset",
    "stub": true,
    "chip": "esp32"
  }
}`)
	mustWriteFile(t, filepath.Join(root, "bootloader", "bootloader.bin"), "bootloader")
	mustWriteFile(t, filepath.Join(root, "partition_table", "partition-table.bin"), "partition")
	mustWriteFile(t, filepath.Join(root, "froth.bin"), "froth")
}

func firmwareZipFixture(t *testing.T) []byte {
	t.Helper()

	tempDir := t.TempDir()
	zipPath := filepath.Join(tempDir, "firmware.zip")

	file, err := os.Create(zipPath)
	if err != nil {
		t.Fatalf("create zip: %v", err)
	}

	writer := zip.NewWriter(file)
	writeZipEntry := func(name string, data string) {
		t.Helper()
		entry, err := writer.Create(name)
		if err != nil {
			t.Fatalf("create zip entry %s: %v", name, err)
		}
		if _, err := entry.Write([]byte(data)); err != nil {
			t.Fatalf("write zip entry %s: %v", name, err)
		}
	}

	writeZipEntry("flasher_args.json", `{
  "write_flash_args": ["--flash_mode", "dio", "--flash_size", "4MB", "--flash_freq", "40m"],
  "flash_files": {
    "0x1000": "bootloader/bootloader.bin",
    "0x10000": "froth.bin",
    "0x8000": "partition_table/partition-table.bin"
  },
  "extra_esptool_args": {
    "before": "default_reset",
    "after": "hard_reset",
    "stub": true,
    "chip": "esp32"
  }
}`)
	writeZipEntry("bootloader/bootloader.bin", "bootloader")
	writeZipEntry("partition_table/partition-table.bin", "partition")
	writeZipEntry("froth.bin", "froth")

	if err := writer.Close(); err != nil {
		t.Fatalf("close zip writer: %v", err)
	}
	if err := file.Close(); err != nil {
		t.Fatalf("close zip file: %v", err)
	}

	data, err := os.ReadFile(zipPath)
	if err != nil {
		t.Fatalf("read zip: %v", err)
	}
	return data
}

func releaseServer(t *testing.T, version string, archive []byte, checksums string) *httptest.Server {
	t.Helper()

	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case fmt.Sprintf("/download/v%s/%s", version, firmwareZipAssetName(version)):
			w.Write(archive)
		case fmt.Sprintf("/download/v%s/%s", version, checksumsAssetName(version)):
			w.Write([]byte(checksums))
		default:
			http.NotFound(w, r)
		}
	}))
}

func sha256Hex(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}
