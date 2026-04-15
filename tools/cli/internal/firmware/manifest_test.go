package firmware

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestArchivePathsSortsAndDeduplicates(t *testing.T) {
	root := t.TempDir()
	writeFixtureFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {
    "0x10000": "froth.bin",
    "0x1000": "bootloader/bootloader.bin",
    "0x8000": "partition_table/partition-table.bin",
    "0x20000": "froth.bin"
  },
  "extra_esptool_args": {"chip": "esp32"}
}`)
	writeFixtureFile(t, filepath.Join(root, "bootloader", "bootloader.bin"), "bootloader")
	writeFixtureFile(t, filepath.Join(root, "partition_table", "partition-table.bin"), "partition")
	writeFixtureFile(t, filepath.Join(root, "froth.bin"), "froth")

	manifest, err := LoadManifest(filepath.Join(root, "flasher_args.json"))
	if err != nil {
		t.Fatalf("LoadManifest() error = %v", err)
	}

	got, err := ArchivePaths(root, manifest)
	if err != nil {
		t.Fatalf("ArchivePaths() error = %v", err)
	}

	want := []string{
		"flasher_args.json",
		"bootloader/bootloader.bin",
		"froth.bin",
		"partition_table/partition-table.bin",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("ArchivePaths() = %v, want %v", got, want)
	}
}

func TestArchivePathsRejectsEscapingPath(t *testing.T) {
	root := t.TempDir()
	writeFixtureFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {"0x1000": "../evil.bin"},
  "extra_esptool_args": {"chip": "esp32"}
}`)

	manifest, err := LoadManifest(filepath.Join(root, "flasher_args.json"))
	if err != nil {
		t.Fatalf("LoadManifest() error = %v", err)
	}

	_, err = ArchivePaths(root, manifest)
	if err == nil {
		t.Fatal("ArchivePaths() succeeded, want error")
	}
	if got := err.Error(); got != "path escapes firmware root: ../evil.bin" {
		t.Fatalf("ArchivePaths() error = %q, want path escape rejection", got)
	}
}

func TestArchivePathsRejectsSymlinkedArtifact(t *testing.T) {
	root := t.TempDir()
	outside := filepath.Join(t.TempDir(), "outside.bin")
	writeFixtureFile(t, outside, "outside")
	if err := os.Symlink(outside, filepath.Join(root, "froth.bin")); err != nil {
		t.Skipf("os.Symlink: %v", err)
	}
	writeFixtureFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {"0x1000": "froth.bin"},
  "extra_esptool_args": {"chip": "esp32"}
}`)

	manifest, err := LoadManifest(filepath.Join(root, "flasher_args.json"))
	if err != nil {
		t.Fatalf("LoadManifest() error = %v", err)
	}

	_, err = ArchivePaths(root, manifest)
	if err == nil {
		t.Fatal("ArchivePaths() succeeded, want error")
	}
	if got := err.Error(); got != "froth.bin must not be a symlink" {
		t.Fatalf("ArchivePaths() error = %q, want symlink rejection", got)
	}
}

func TestArchivePathsRejectsSymlinkedDirectoryEscape(t *testing.T) {
	root := t.TempDir()
	outsideDir := filepath.Join(t.TempDir(), "outside")
	writeFixtureFile(t, filepath.Join(outsideDir, "bootloader.bin"), "outside")
	if err := os.Symlink(outsideDir, filepath.Join(root, "bootloader")); err != nil {
		t.Skipf("os.Symlink: %v", err)
	}
	writeFixtureFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {"0x1000": "bootloader/bootloader.bin"},
  "extra_esptool_args": {"chip": "esp32"}
}`)

	manifest, err := LoadManifest(filepath.Join(root, "flasher_args.json"))
	if err != nil {
		t.Fatalf("LoadManifest() error = %v", err)
	}

	_, err = ArchivePaths(root, manifest)
	if err == nil {
		t.Fatal("ArchivePaths() succeeded, want error")
	}
	if got := err.Error(); got != "path escapes firmware root: bootloader/bootloader.bin" {
		t.Fatalf("ArchivePaths() error = %q, want symlink directory rejection", got)
	}
}

func TestArchivePathsCanonicalizesAndDeduplicatesPaths(t *testing.T) {
	root := t.TempDir()
	writeFixtureFile(t, filepath.Join(root, "flasher_args.json"), `{
  "write_flash_args": ["--flash_mode", "dio"],
  "flash_files": {
    "0x1000": "./bootloader/../froth.bin",
    "0x2000": "froth.bin",
    "0x3000": ""
  },
  "extra_esptool_args": {"chip": "esp32"}
}`)
	writeFixtureFile(t, filepath.Join(root, "froth.bin"), "froth")

	manifest, err := LoadManifest(filepath.Join(root, "flasher_args.json"))
	if err != nil {
		t.Fatalf("LoadManifest() error = %v", err)
	}

	got, err := ArchivePaths(root, manifest)
	if err != nil {
		t.Fatalf("ArchivePaths() error = %v", err)
	}

	want := []string{"flasher_args.json", "froth.bin"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("ArchivePaths() = %v, want %v", got, want)
	}
}

func TestOrderedFilesSortsOffsets(t *testing.T) {
	manifest := &Manifest{
		FlashFiles: map[string]string{
			"0x10000": "froth.bin",
			"0x1000":  "bootloader/bootloader.bin",
			"0x8000":  "partition_table/partition-table.bin",
		},
	}
	manifest.ExtraEsptoolArgs.Chip = "esp32"

	got, err := OrderedFiles(manifest)
	if err != nil {
		t.Fatalf("OrderedFiles() error = %v", err)
	}

	if len(got) != 3 {
		t.Fatalf("len(OrderedFiles()) = %d, want 3", len(got))
	}
	if got[0].Offset != "0x1000" || got[1].Offset != "0x8000" || got[2].Offset != "0x10000" {
		t.Fatalf("OrderedFiles() = %+v, want offsets sorted", got)
	}
}

func writeFixtureFile(t *testing.T, path string, data string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("MkdirAll(%s): %v", path, err)
	}
	if err := os.WriteFile(path, []byte(data), 0o644); err != nil {
		t.Fatalf("WriteFile(%s): %v", path, err)
	}
}
