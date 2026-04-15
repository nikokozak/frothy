package cmd

import (
	"path/filepath"
	"testing"
)

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
