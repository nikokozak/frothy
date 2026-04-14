package cmd

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/project"
)

func runNew(args []string) error {
	if len(args) == 0 || strings.TrimSpace(args[0]) == "" {
		return fmt.Errorf("new requires a project name")
	}
	dir := args[0]
	name := filepath.Base(dir)
	if name == "" || name == "." || name == "/" {
		return fmt.Errorf("invalid project name: %q", args[0])
	}

	// Reject names that would produce invalid TOML
	if strings.ContainsAny(name, "\"'\n\r\\") {
		return fmt.Errorf("project name %q contains invalid characters", name)
	}

	board, platform, err := scaffoldTargetConfig(targetFlag)
	if err != nil {
		return err
	}

	if _, err := os.Stat(dir); err == nil {
		return fmt.Errorf("directory %s already exists", dir)
	}

	manifest := fmt.Sprintf(`[project]
name = "%s"
version = "0.1.0"
entry = "src/main.froth"

[target]
board = "%s"
platform = "%s"

# Example named dependency:
# [dependencies]
# utils = { path = "lib/utils.froth" }
#
# In Frothy source:
# \ #use "utils"
#
# Relative includes also work without a manifest entry:
# \ #use "../lib/utils.froth"

# This [target] block is the project authority after scaffolding.
# froth new defaults to posix.
# froth new --target <board> can prefill a non-posix target.
# You can also edit these values later.

# Project FFI: compile your own C bindings alongside the kernel.
# Preferred: export a null-terminated frothy_ffi_entry_t[] table
# named frothy_project_bindings.
# For arity > 0 entries, set .param_count = FROTHY_FFI_PARAM_COUNT(params).
# Compatibility: legacy froth_ffi_entry_t[] froth_project_bindings also works.
# Optional: set .stack_effect on each frothy_ffi_entry_t if you want
# slotInfo to show an explicit effect line for maintained bindings.
#
# [ffi]
# sources = ["src/ffi/bindings.c"]
# includes = ["src/ffi"]
# defines = { MY_CONSTANT = "42" }
`, name, board, platform)

	files := map[string]string{
		filepath.Join(dir, "froth.toml"):      manifest,
		filepath.Join(dir, "lib", ".gitkeep"): "",
		filepath.Join(dir, ".gitignore"): `.froth-build/
froth_a.snap
froth_b.snap
`,
	}
	for relPath, content := range project.StarterSourceFiles(board) {
		files[filepath.Join(dir, relPath)] = content
	}

	dirs := map[string]struct{}{
		dir:                       {},
		filepath.Join(dir, "lib"): {},
	}
	for path := range files {
		dirs[filepath.Dir(path)] = struct{}{}
	}
	for d := range dirs {
		if err := os.MkdirAll(d, 0755); err != nil {
			return fmt.Errorf("create %s: %w", d, err)
		}
	}

	for path, content := range files {
		if err := os.WriteFile(path, []byte(content), 0644); err != nil {
			return fmt.Errorf("write %s: %w", path, err)
		}
	}

	fmt.Printf("Created project %s\n", name)
	fmt.Printf("  target: %s (%s)\n", board, platform)
	fmt.Printf("  entry:  src/main.froth\n")
	fmt.Println()
	fmt.Printf("Next steps:\n")
	fmt.Printf("  cd %s\n", dir)
	if platform == "esp-idf" {
		fmt.Printf("  froth doctor      # verify CLI, ESP-IDF, and serial path\n")
		fmt.Printf("  froth send        # send to a preflashed board\n")
		fmt.Printf("  froth build       # build firmware\n")
		fmt.Printf("  froth flash       # flash firmware and apply runtime\n")
	} else {
		fmt.Printf("  froth send        # send to device\n")
		fmt.Printf("  froth build       # build firmware\n")
	}

	return nil
}

func scaffoldTargetConfig(flag string) (board string, platform string, err error) {
	name := strings.TrimSpace(flag)
	if name == "" {
		return "posix", "posix", nil
	}

	if !isShellSafe(name) {
		return "", "", fmt.Errorf("invalid board target: %s", name)
	}

	kernelRoot, err := findKernelRoot()
	if err != nil {
		return "", "", fmt.Errorf("resolve board target %q: %w", name, err)
	}

	boardPath := filepath.Join(kernelRoot, "boards", name, "board.json")
	data, err := os.ReadFile(boardPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", "", fmt.Errorf("unknown board target: %s", name)
		}
		return "", "", fmt.Errorf("read board target %q: %w", name, err)
	}

	var meta struct {
		Platform string `json:"platform"`
	}
	if err := json.Unmarshal(data, &meta); err != nil {
		return "", "", fmt.Errorf("parse board target %q: %w", name, err)
	}
	if strings.TrimSpace(meta.Platform) == "" {
		return "", "", fmt.Errorf("board target %q missing platform", name)
	}

	return name, meta.Platform, nil
}
