package cmd

import (
	"fmt"
	"os"

	"github.com/nikokozak/frothy/tools/cli/internal/frothycontrol"
)

type lookPathFunc func(string) (string, error)

func findMakeTool(lookPath lookPathFunc) (string, string, error) {
	for _, candidate := range []string{"make", "gmake"} {
		path, err := lookPath(candidate)
		if err == nil {
			return path, candidate, nil
		}
	}

	return "", "", fmt.Errorf("GNU Make not found on PATH")
}

func runTooling(args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("unknown tooling command")
	}

	switch args[0] {
	case "resolve-source":
		fileArg := ""
		if len(args) >= 2 {
			fileArg = args[1]
		}
		if len(args) > 2 {
			return fmt.Errorf("usage: froth tooling resolve-source [file]")
		}
		return runToolingResolveSource(fileArg)
	case "control-smoke":
		return runToolingControlSmoke(args[1:])
	case "control-session":
		return runToolingControlSession(args[1:])
	default:
		return fmt.Errorf("unknown tooling command: %s", args[0])
	}
}

func runToolingResolveSource(fileArg string) error {
	payload, err := resolveSource(fileArg)
	if err != nil {
		return err
	}

	_, err = os.Stdout.WriteString(payload.source)
	return err
}

func runToolingControlSmoke(args []string) error {
	cfg := frothycontrol.SmokeConfig{
		Port: portFlag,
	}

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--local-runtime":
			if i+1 >= len(args) {
				return fmt.Errorf("missing value for --local-runtime")
			}
			cfg.LocalRuntime = args[i+1]
			i++
		default:
			return fmt.Errorf("unknown control-smoke flag: %s", args[i])
		}
	}

	if cfg.LocalRuntime == "" && cfg.Port == "" {
		return fmt.Errorf("control-smoke requires --port or --local-runtime")
	}
	if cfg.LocalRuntime != "" && cfg.Port != "" {
		return fmt.Errorf("control-smoke accepts only one of --port or --local-runtime")
	}

	return frothycontrol.RunSmoke(cfg)
}

func runToolingControlSession(args []string) error {
	config := frothycontrol.ManagerConfig{
		DefaultPort: portFlag,
	}

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--local-runtime":
			if i+1 >= len(args) {
				return fmt.Errorf("missing value for --local-runtime")
			}
			config.LocalRuntimePath = args[i+1]
			i++
		default:
			return fmt.Errorf("unknown control-session flag: %s", args[i])
		}
	}

	manager := frothycontrol.NewManager(config)
	return frothycontrol.RunControlSessionServer(manager, os.Stdin, os.Stdout)
}
