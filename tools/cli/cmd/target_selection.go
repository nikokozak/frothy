package cmd

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/nikokozak/frothy/tools/cli/internal/project"
)

type cliSelection struct {
	Platform       string
	Board          string
	ExplicitTarget bool
	ExplicitBoard  bool
}

func (s cliSelection) hasExplicitSelection() bool {
	return s.ExplicitTarget || s.ExplicitBoard
}

func (s cliSelection) flagSummary() string {
	var parts []string
	if s.ExplicitTarget {
		parts = append(parts, fmt.Sprintf("--target %s", targetFlag))
	}
	if s.ExplicitBoard {
		parts = append(parts, fmt.Sprintf("--board %s", boardFlag))
	}
	if len(parts) == 0 {
		return ""
	}
	return strings.Join(parts, " ")
}

func noteManifestSelectionOverride(manifest *project.Manifest) {
	selection := cliSelection{
		ExplicitTarget: targetFlag != "",
		ExplicitBoard:  boardFlag != "",
	}
	if !selection.hasExplicitSelection() {
		return
	}

	fmt.Fprintf(os.Stderr,
		"note: ignoring %s because froth.toml selects %s (%s)\n",
		selection.flagSummary(),
		manifest.Target.Board,
		manifest.Target.Platform,
	)
}

func resolveLegacyCLISelection(kernelRoot string) (cliSelection, error) {
	selection := cliSelection{
		ExplicitTarget: targetFlag != "",
		ExplicitBoard:  boardFlag != "",
	}

	platform, err := resolvePlatformFlag(kernelRoot, targetFlag)
	if err != nil {
		return cliSelection{}, err
	}

	board, boardPlatform, err := resolveBoardFlag(kernelRoot, boardFlag)
	if err != nil {
		return cliSelection{}, err
	}

	switch {
	case board != "" && platform == "":
		platform = boardPlatform
	case board != "" && platform != boardPlatform:
		return cliSelection{}, fmt.Errorf(
			"board %s targets platform %s, not %s",
			board,
			boardPlatform,
			platform,
		)
	}

	if platform == "" {
		platform = "posix"
	}
	if platform == "posix" && board == "" {
		board = "posix"
	}

	selection.Platform = platform
	selection.Board = board
	return selection, nil
}

func resolveNewCLISelection(kernelRoot string) (cliSelection, error) {
	selection, err := resolveLegacyCLISelection(kernelRoot)
	if err != nil {
		if targetFlag != "" && boardFlag == "" {
			board, platform, boardErr := resolveBoardFlag(kernelRoot, targetFlag)
			if boardErr == nil {
				fmt.Fprintf(os.Stderr,
					"note: `%s new --target <board>` is deprecated; use `--board %s` instead\n",
					cliCommandName,
					targetFlag,
				)
				return cliSelection{
					Platform:       platform,
					Board:          board,
					ExplicitTarget: false,
					ExplicitBoard:  true,
				}, nil
			}
		}
		return cliSelection{}, err
	}

	if selection.Platform == "esp-idf" && selection.Board == "" {
		return cliSelection{}, fmt.Errorf("`%s new --target esp-idf` requires --board <name>", cliCommandName)
	}

	return selection, nil
}

func resolvePlatformFlag(kernelRoot string, flag string) (string, error) {
	name := strings.TrimSpace(flag)
	if name == "" {
		return "", nil
	}
	if !isShellSafe(name) {
		return "", fmt.Errorf("invalid target platform: %s", name)
	}

	switch name {
	case "posix", "esp-idf":
		return name, nil
	}

	if _, _, err := resolveBoardFlag(kernelRoot, name); err == nil {
		return "", fmt.Errorf("`--target` expects a platform (`posix` or `esp-idf`); use `--board %s` instead", name)
	}

	return "", fmt.Errorf("unknown target platform: %s", name)
}

func resolveBoardFlag(kernelRoot string, flag string) (string, string, error) {
	name := strings.TrimSpace(flag)
	if name == "" {
		return "", "", nil
	}
	if !isShellSafe(name) {
		return "", "", fmt.Errorf("invalid board name: %s", name)
	}

	boardPath := filepath.Join(kernelRoot, "boards", name, "board.json")
	data, err := os.ReadFile(boardPath)
	if err != nil {
		if os.IsNotExist(err) {
			return "", "", fmt.Errorf("unknown board: %s", name)
		}
		return "", "", fmt.Errorf("read board %q: %w", name, err)
	}

	var meta struct {
		Platform string `json:"platform"`
	}
	if err := json.Unmarshal(data, &meta); err != nil {
		return "", "", fmt.Errorf("parse board %q: %w", name, err)
	}
	if strings.TrimSpace(meta.Platform) == "" {
		return "", "", fmt.Errorf("board %q missing platform", name)
	}

	return name, meta.Platform, nil
}

func cleanBuildDirForExplicitSelection(dir string, selection cliSelection) error {
	if selection.hasExplicitSelection() {
		cleaned, err := removeBuildDirIfPresent(dir)
		if err != nil {
			return err
		}
		if cleaned {
			fmt.Fprintf(os.Stderr,
				"note: cleaned build directory to avoid sticky target/board cache for %s\n",
				selection.flagSummary(),
			)
			if cleanFlag {
				fmt.Println("Cleaned build directory")
			}
		}
	}

	return cleanBuildDirIfRequested(dir)
}

func removeBuildDirIfPresent(dir string) (bool, error) {
	if _, err := os.Stat(dir); err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return false, fmt.Errorf("check build dir: %w", err)
	}

	if err := os.RemoveAll(dir); err != nil {
		return false, fmt.Errorf("remove build dir: %w", err)
	}

	return true, nil
}
