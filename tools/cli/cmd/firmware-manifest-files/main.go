package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/nikokozak/frothy/tools/cli/internal/firmware"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "usage: %s <flasher_args.json>\n", filepath.Base(os.Args[0]))
		os.Exit(2)
	}

	manifestPath := os.Args[1]
	manifest, err := firmware.LoadManifest(manifestPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	files, err := firmware.ArchivePaths(filepath.Dir(manifestPath), manifest)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	for _, relPath := range files {
		fmt.Println(relPath)
	}
}
