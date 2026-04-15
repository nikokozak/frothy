package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/nikokozak/frothy/tools/cli/internal/project"
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

	board, platform, err := scaffoldTargetConfig()
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
# frothy new defaults to posix.
# frothy new --board <board> can prefill a non-posix target.
# You can also edit these values later.

# Project FFI: compile your own C bindings alongside the kernel.
# Preferred: export a null-terminated frothy_ffi_entry_t[] table
# named frothy_project_bindings.
# For arity > 0 entries, set .param_count = FROTHY_FFI_PARAM_COUNT(params).
# Retained compatibility only: legacy froth_ffi_entry_t[] froth_project_bindings
# still works, but new code should start on the maintained frothy_ffi path.
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
		fmt.Printf("  %s doctor      # verify CLI, ESP-IDF, and serial path\n", cliCommandName)
		fmt.Printf("  %s send        # send to a preflashed board\n", cliCommandName)
		fmt.Printf("  %s build       # build firmware\n", cliCommandName)
		fmt.Printf("  %s flash       # flash firmware and apply runtime\n", cliCommandName)
	} else {
		fmt.Printf("  %s send        # send to device\n", cliCommandName)
		fmt.Printf("  %s build       # build firmware\n", cliCommandName)
	}

	return nil
}

func scaffoldTargetConfig() (board string, platform string, err error) {
	kernelRoot, err := findKernelRoot()
	if err != nil {
		return "", "", fmt.Errorf("resolve scaffold target: %w", err)
	}

	selection, err := resolveNewCLISelection(kernelRoot)
	if err != nil {
		return "", "", err
	}

	if selection.Platform == "" || selection.Platform == "posix" {
		if selection.Board == "" {
			return "posix", "posix", nil
		}
	}

	return selection.Board, selection.Platform, nil
}
