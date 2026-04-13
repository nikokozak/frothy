package cmd

import (
	"fmt"
	"os"

	"github.com/nikokozak/froth/tools/cli/internal/sdk"
)

// Global flags
var (
	portFlag   string
	targetFlag string
	cleanFlag  bool
)

var runSetupCommand = runSetup

// Execute parses os.Args and dispatches to the right subcommand.
func Execute() error {
	args := os.Args[1:]
	var remaining []string

	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--port":
			if i+1 < len(args) {
				portFlag = args[i+1]
				i++
			}
		case "--target":
			if i+1 < len(args) {
				targetFlag = args[i+1]
				i++
			}
		case "--clean":
			cleanFlag = true
		case "--version":
			version, err := cliVersion()
			if err != nil {
				return err
			}
			fmt.Printf("froth %s\n", version)
			return nil
		default:
			remaining = append(remaining, args[i])
		}
	}

	if len(remaining) == 0 {
		printUsage()
		return nil
	}

	switch remaining[0] {
	case "new":
		return runNew(remaining[1:])
	case "info":
		return runInfo()
	case "connect":
		return runConnect(remaining[1:])
	case "send":
		fileArg := ""
		if len(remaining) >= 2 {
			fileArg = remaining[1]
		}
		return runSend(fileArg)
	case "doctor":
		return runDoctor()
	case "build":
		return runBuild()
	case "flash":
		return runFlash()
	case "setup":
		return runSetupCommand(remaining[1:])
	case "reset":
		return runReset()
	case "tooling":
		return runTooling(remaining[1:])
	default:
		return fmt.Errorf("unknown command: %s", remaining[0])
	}
}

func printUsage() {
	fmt.Println("Usage: froth [flags] <command>")
	fmt.Println()
	fmt.Println("Commands:")
	fmt.Println("  new <name>      Create a new Frothy project")
	fmt.Println("  doctor          Check environment and device")
	fmt.Println("  build           Build Frothy firmware")
	fmt.Println("  flash           Flash device")
	fmt.Println("  setup           Install optional toolchains")
	fmt.Println("  connect         Connect to Frothy")
	fmt.Println("  send [file]     Send source to a Frothy runtime (resolves includes)")
	fmt.Println("  info            Show device info")
	fmt.Println("  reset           Wipe the saved and live overlay image")
	fmt.Println()
	fmt.Println("Flags:")
	fmt.Println("  --port <path>    Serial port (auto-detect if omitted)")
	fmt.Println("  --target <name>  Scaffold target for `new`; legacy selector for non-project build/flash paths")
	fmt.Println("  --clean          Delete the build directory before building")
	fmt.Println("  --version        Print Frothy version and exit")
}

func cliVersion() (string, error) {
	payloadRoot, err := sdk.EmbeddedPayloadRoot()
	if err != nil {
		return "", err
	}
	return sdk.VersionFromFS(payloadRoot)
}
