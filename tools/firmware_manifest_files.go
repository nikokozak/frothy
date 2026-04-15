package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"slices"
)

type flasherManifest struct {
	FlashFiles map[string]string `json:"flash_files"`
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "usage: %s <flasher_args.json>\n", filepath.Base(os.Args[0]))
		os.Exit(2)
	}

	data, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "read manifest: %v\n", err)
		os.Exit(1)
	}

	var manifest flasherManifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		fmt.Fprintf(os.Stderr, "parse manifest: %v\n", err)
		os.Exit(1)
	}

	if len(manifest.FlashFiles) == 0 {
		fmt.Fprintln(os.Stderr, "manifest missing flash_files")
		os.Exit(1)
	}

	seen := map[string]struct{}{
		"flasher_args.json": {},
	}
	files := []string{"flasher_args.json"}
	for _, path := range manifest.FlashFiles {
		if path == "" {
			continue
		}
		if _, ok := seen[path]; ok {
			continue
		}
		seen[path] = struct{}{}
		files = append(files, path)
	}
	slices.Sort(files[1:])

	for _, path := range files {
		fmt.Println(path)
	}
}
