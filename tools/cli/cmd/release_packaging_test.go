package cmd

import (
	"archive/tar"
	"archive/zip"
	"compress/gzip"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"testing"
)

func TestPackageReleaseScriptIncludesBinaryAndReadme(t *testing.T) {
	repoRoot := repoRootForScriptTest(t)
	workDir := t.TempDir()
	binaryPath := filepath.Join(workDir, "froth-cli")
	if err := os.WriteFile(binaryPath, []byte("#!/bin/sh\nexit 0\n"), 0o755); err != nil {
		t.Fatalf("write binary: %v", err)
	}

	outputDir := filepath.Join(workDir, "dist")
	cmd := exec.Command(
		filepath.Join(repoRoot, "tools", "package-release.sh"),
		binaryPath,
		"0.1.0",
		"darwin",
		"arm64",
		outputDir,
	)
	cmd.Dir = repoRoot
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("package-release.sh: %v\n%s", err, output)
	}

	archivePath := filepath.Join(outputDir, "frothy-v0.1.0-darwin-arm64.tar.gz")
	entries := untarEntries(t, archivePath)
	if _, ok := entries["froth"]; !ok {
		t.Fatalf("archive entries = %v, want froth", sortedKeys(entries))
	}
	readme, ok := entries["README.txt"]
	if !ok {
		t.Fatalf("archive entries = %v, want README.txt", sortedKeys(entries))
	}
	if !strings.Contains(readme, "Installed command: froth") {
		t.Fatalf("README.txt = %q, want installed command note", readme)
	}
}

func TestPackageFirmwareReleaseScriptIncludesManifestArtifacts(t *testing.T) {
	repoRoot := repoRootForScriptTest(t)
	buildDir := t.TempDir()
	writeFirmwareFixture(t, buildDir)

	outputPath := filepath.Join(t.TempDir(), "firmware.zip")
	cmd := exec.Command(
		filepath.Join(repoRoot, "tools", "package-firmware-release.sh"),
		buildDir,
		"0.1.0",
		outputPath,
	)
	cmd.Dir = repoRoot
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("package-firmware-release.sh: %v\n%s", err, output)
	}

	entries := zipEntries(t, outputPath)
	for _, want := range []string{
		"flasher_args.json",
		"bootloader/bootloader.bin",
		"partition_table/partition-table.bin",
		"froth.bin",
	} {
		if _, ok := entries[want]; !ok {
			t.Fatalf("zip entries = %v, want %s", sortedKeys(entries), want)
		}
	}
}

func TestPackageFirmwareReleaseScriptAcceptsRelativeBuildDir(t *testing.T) {
	repoRoot := repoRootForScriptTest(t)
	workDir := t.TempDir()
	buildDir := filepath.Join(workDir, "build")
	writeFirmwareFixture(t, buildDir)

	outputPath := filepath.Join(workDir, "firmware.zip")
	cmd := exec.Command(
		filepath.Join(repoRoot, "tools", "package-firmware-release.sh"),
		"build",
		"0.1.0",
		outputPath,
	)
	cmd.Dir = workDir
	output, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("package-firmware-release.sh relative build dir: %v\n%s", err, output)
	}

	entries := zipEntries(t, outputPath)
	if _, ok := entries["flasher_args.json"]; !ok {
		t.Fatalf("zip entries = %v, want flasher_args.json", sortedKeys(entries))
	}
}

func TestPackageFirmwareReleaseScriptFailsWhenManifestArtifactMissing(t *testing.T) {
	repoRoot := repoRootForScriptTest(t)
	buildDir := t.TempDir()
	writeFirmwareFixture(t, buildDir)
	if err := os.Remove(filepath.Join(buildDir, "froth.bin")); err != nil {
		t.Fatalf("remove firmware artifact: %v", err)
	}

	outputPath := filepath.Join(t.TempDir(), "firmware.zip")
	cmd := exec.Command(
		filepath.Join(repoRoot, "tools", "package-firmware-release.sh"),
		buildDir,
		"0.1.0",
		outputPath,
	)
	cmd.Dir = repoRoot
	output, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("package-firmware-release.sh succeeded, want failure\n%s", output)
	}
	if !strings.Contains(string(output), "missing froth.bin") {
		t.Fatalf("package-release output = %q, want missing artifact error", output)
	}
}

func repoRootForScriptTest(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	return filepath.Clean(filepath.Join(filepath.Dir(file), "..", "..", ".."))
}

func untarEntries(t *testing.T, archivePath string) map[string]string {
	t.Helper()
	file, err := os.Open(archivePath)
	if err != nil {
		t.Fatalf("open tarball: %v", err)
	}
	defer file.Close()

	gzipReader, err := gzip.NewReader(file)
	if err != nil {
		t.Fatalf("gzip reader: %v", err)
	}
	defer gzipReader.Close()

	reader := tar.NewReader(gzipReader)
	entries := make(map[string]string)
	for {
		header, err := reader.Next()
		if err != nil {
			if err == io.EOF {
				return entries
			}
			t.Fatalf("tar next: %v", err)
		}
		data, err := io.ReadAll(reader)
		if err != nil {
			t.Fatalf("read tar entry %s: %v", header.Name, err)
		}
		entries[header.Name] = string(data)
	}
}

func zipEntries(t *testing.T, archivePath string) map[string]string {
	t.Helper()
	reader, err := zip.OpenReader(archivePath)
	if err != nil {
		t.Fatalf("open zip: %v", err)
	}
	defer reader.Close()

	entries := make(map[string]string)
	for _, file := range reader.File {
		handle, err := file.Open()
		if err != nil {
			t.Fatalf("open zip entry %s: %v", file.Name, err)
		}
		data, err := io.ReadAll(handle)
		_ = handle.Close()
		if err != nil {
			t.Fatalf("read zip entry %s: %v", file.Name, err)
		}
		entries[file.Name] = string(data)
	}
	return entries
}

func sortedKeys(values map[string]string) []string {
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}
