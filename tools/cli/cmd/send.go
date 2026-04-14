package cmd

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
	"github.com/nikokozak/froth/tools/cli/internal/project"
	baseproto "github.com/nikokozak/froth/tools/cli/internal/protocol"
)

type sendRuntime interface {
	controlEvalManager
	Reset() (*baseproto.ResetResponse, error)
	Words() ([]string, error)
}

var sendControlSource = runControlSource
var sendControlEval = runControlEval

type sendPayload struct {
	source          string
	resetBeforeEval bool
}

func runSend(fileArg string) error {
	payload, err := resolveSource(fileArg)
	if err != nil {
		return err
	}

	manager := frothycontrol.NewManager(frothycontrol.ManagerConfig{
		DefaultPort: portFlag,
	})
	if _, err := manager.Connect(""); err != nil {
		return err
	}
	defer manager.Disconnect()

	return sendResolvedPayload(manager, payload)
}

func sendResolvedPayload(manager sendRuntime, payload *sendPayload) error {
	if payload.resetBeforeEval {
		if _, err := manager.Reset(); err != nil {
			if errors.Is(err, frothycontrol.ErrResetUnavailable) {
				return fmt.Errorf("whole-file send requires reset + eval; connected firmware is too old for safe whole-file send: %w", err)
			}
			return fmt.Errorf("reset(): %w", err)
		}
	}

	value, err := sendControlSource(manager, payload.source)
	if err != nil {
		return fmt.Errorf("eval: %w", err)
	}
	printControlValue(value)

	if !payload.resetBeforeEval {
		return nil
	}

	words, err := manager.Words()
	if err != nil {
		return fmt.Errorf("words: %w", err)
	}

	switch {
	case contains(words, "boot"):
		value, err = sendControlEval(manager, "boot:")
	case contains(words, "autorun"):
		value, err = sendControlEval(manager, "autorun:")
	default:
		return nil
	}
	if err != nil {
		return fmt.Errorf("run entrypoint: %w", err)
	}
	printControlValue(value)
	return nil
}

// resolveSource resolves includes and produces a merged source string.
// If fileArg is a raw .frothy source string (not a file path), it's sent directly.
// If fileArg is a file path, the resolver runs. If no fileArg, uses froth.toml entry.
func resolveSource(fileArg string) (*sendPayload, error) {
	if fileArg != "" {
		info, err := os.Stat(fileArg)
		if err == nil {
			if info.IsDir() {
				return nil, fmt.Errorf("%s is a directory, not a file", fileArg)
			}
			return resolveFromFile(fileArg)
		}
		if strings.HasSuffix(fileArg, ".froth") || strings.HasSuffix(fileArg, ".frothy") || strings.Contains(fileArg, "/") {
			return nil, fmt.Errorf("file not found: %s", fileArg)
		}
		return &sendPayload{source: fileArg}, nil
	}

	cwd, err := os.Getwd()
	if err != nil {
		return nil, fmt.Errorf("working directory: %w", err)
	}

	manifest, root, err := project.Load(cwd)
	if err != nil {
		return nil, fmt.Errorf("no file specified and %w", err)
	}

	result, err := project.Resolve(manifest, root)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	printResolveSummary(result)

	return &sendPayload{
		source:          project.StripBoundaryMarkers(result.Source),
		resetBeforeEval: true,
	}, nil
}

func resolveFromFile(filePath string) (*sendPayload, error) {
	absPath, err := filepath.Abs(filePath)
	if err != nil {
		return nil, err
	}

	fileDir := filepath.Dir(absPath)
	manifest, root, err := project.Load(fileDir)
	if err != nil {
		return resolveBareSingleFile(absPath)
	}

	relPath, err := filepath.Rel(root, absPath)
	if err != nil {
		return resolveBareSingleFile(absPath)
	}
	manifest.Project.Entry = relPath

	result, err := project.Resolve(manifest, root)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	printResolveSummary(result)

	return &sendPayload{
		source:          project.StripBoundaryMarkers(result.Source),
		resetBeforeEval: true,
	}, nil
}

func resolveBareSingleFile(absPath string) (*sendPayload, error) {
	root := filepath.Dir(absPath)
	if cwd, err := os.Getwd(); err == nil {
		canonPath, pathErr := filepath.EvalSymlinks(absPath)
		canonCwd, cwdErr := filepath.EvalSymlinks(cwd)
		if pathErr == nil && cwdErr == nil {
			if canonPath == canonCwd ||
				strings.HasPrefix(canonPath, canonCwd+string(filepath.Separator)) {
				root = cwd
			}
		}
	}

	result, err := project.ResolveEntry(absPath, root)
	if err != nil {
		return nil, err
	}
	printWarnings(result.Warnings)
	return &sendPayload{
		source:          project.StripBoundaryMarkers(result.Source),
		resetBeforeEval: true,
	}, nil
}

func printWarnings(warnings []string) {
	for _, w := range warnings {
		fmt.Fprintf(os.Stderr, "warning: %s\n", w)
	}
}

func printResolveSummary(result *project.ResolveResult) {
	if len(result.Files) > 1 {
		fmt.Fprintf(os.Stderr, "Resolved %s (%d dependencies)\n",
			result.Files[len(result.Files)-1], len(result.Files)-1)
	}
}
