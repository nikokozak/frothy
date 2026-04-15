package firmware

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type Manifest struct {
	WriteFlashArgs   []string          `json:"write_flash_args"`
	FlashFiles       map[string]string `json:"flash_files"`
	ExtraEsptoolArgs struct {
		After  string `json:"after"`
		Before string `json:"before"`
		Stub   *bool  `json:"stub"`
		Chip   string `json:"chip"`
	} `json:"extra_esptool_args"`
}

type FlashFile struct {
	Offset string
	Path   string
	Value  uint64
}

func LoadManifest(path string) (*Manifest, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read flasher_args.json: %w", err)
	}

	var manifest Manifest
	if err := json.Unmarshal(data, &manifest); err != nil {
		return nil, fmt.Errorf("parse flasher_args.json: %w", err)
	}
	if err := ValidateManifest(&manifest); err != nil {
		return nil, err
	}
	return &manifest, nil
}

func ValidateManifest(manifest *Manifest) error {
	if manifest == nil {
		return fmt.Errorf("manifest is nil")
	}
	if manifest.ExtraEsptoolArgs.Chip == "" {
		return fmt.Errorf("missing chip in flasher_args.json")
	}
	if len(manifest.FlashFiles) == 0 {
		return fmt.Errorf("missing flash_files in flasher_args.json")
	}
	return nil
}

func ValidateFiles(root string, manifest *Manifest) error {
	if err := ValidateManifest(manifest); err != nil {
		return err
	}

	for _, relPath := range manifest.FlashFiles {
		cleanRel := canonicalRelPath(relPath)
		if cleanRel == "" {
			continue
		}
		if _, err := RegularFilePath(root, cleanRel); err != nil {
			return err
		}
	}

	return nil
}

func RegularFilePath(root string, rel string) (string, error) {
	fullPath, err := SafeJoin(root, rel)
	if err != nil {
		return "", err
	}
	info, err := os.Lstat(fullPath)
	if err != nil {
		return "", fmt.Errorf("missing %s: %w", rel, err)
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return "", fmt.Errorf("%s must not be a symlink", rel)
	}

	resolvedRoot, err := filepath.EvalSymlinks(root)
	if err != nil {
		return "", fmt.Errorf("resolve firmware root: %w", err)
	}
	resolvedPath, err := filepath.EvalSymlinks(fullPath)
	if err != nil {
		return "", fmt.Errorf("resolve %s: %w", rel, err)
	}
	if resolvedPath != resolvedRoot &&
		!strings.HasPrefix(resolvedPath, resolvedRoot+string(filepath.Separator)) {
		return "", fmt.Errorf("path escapes firmware root: %s", rel)
	}
	if !info.Mode().IsRegular() {
		return "", fmt.Errorf("%s is not a regular file", rel)
	}

	return fullPath, nil
}

func OrderedFiles(manifest *Manifest) ([]FlashFile, error) {
	if err := ValidateManifest(manifest); err != nil {
		return nil, err
	}

	files := make([]FlashFile, 0, len(manifest.FlashFiles))
	for offset, path := range manifest.FlashFiles {
		cleanPath := canonicalRelPath(path)
		if cleanPath == "" {
			continue
		}
		value, err := strconv.ParseUint(offset, 0, 64)
		if err != nil {
			return nil, fmt.Errorf("invalid flash offset %s: %w", offset, err)
		}
		files = append(files, FlashFile{Offset: offset, Path: cleanPath, Value: value})
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].Value < files[j].Value
	})
	return files, nil
}

func ArchivePaths(root string, manifest *Manifest) ([]string, error) {
	if err := ValidateFiles(root, manifest); err != nil {
		return nil, err
	}

	seen := map[string]struct{}{
		"flasher_args.json": {},
	}
	files := []string{"flasher_args.json"}
	for _, relPath := range manifest.FlashFiles {
		cleanRel := canonicalRelPath(relPath)
		if cleanRel == "" {
			continue
		}
		if _, ok := seen[cleanRel]; ok {
			continue
		}
		seen[cleanRel] = struct{}{}
		files = append(files, cleanRel)
	}
	sort.Strings(files[1:])
	return files, nil
}

func canonicalRelPath(rel string) string {
	if rel == "" {
		return ""
	}
	return filepath.Clean(rel)
}

func SafeJoin(root string, rel string) (string, error) {
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
