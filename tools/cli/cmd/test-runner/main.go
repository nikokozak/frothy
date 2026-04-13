package main

import (
	"fmt"
	"os"
	"path/filepath"
)

type commandFunc func(args []string) error

var commands = map[string]commandFunc{
	"fast":             commandFast,
	"all":              commandAll,
	"frothy":           commandFrothy,
	"cli":              commandCLIUnit,
	"cli-local":        commandCLILocal,
	"integration":      commandCLIIntegration,
	"list":             commandList,
	"ensure-profile":   commandEnsureProfile,
	"corrupt-snapshot": commandCorruptSnapshot,
	"proof-ctrlc":      commandProofCtrlC,
	"proof-safeboot":   commandProofSafeBoot,
}

func main() {
	if len(os.Args) < 2 {
		fatalf("usage: %s <command>", filepath.Base(os.Args[0]))
	}

	command, ok := commands[os.Args[1]]
	if !ok {
		fatalf("error: unknown command: %s", os.Args[1])
	}
	if err := command(os.Args[2:]); err != nil {
		fatalf("error: %v", err)
	}
}

func commandFast(_ []string) error {
	return runFast()
}

func commandAll(_ []string) error {
	return runAll()
}

func commandFrothy(_ []string) error {
	return runFrothy()
}

func commandCLIUnit(_ []string) error {
	return runCLIUnit()
}

func commandCLILocal(_ []string) error {
	return runCLILocal()
}

func commandCLIIntegration(_ []string) error {
	return runCLIIntegration()
}

func commandList(_ []string) error {
	printList()
	return nil
}

func init() {
	for name, command := range commands {
		if command == nil {
			panic(fmt.Sprintf("nil command handler: %s", name))
		}
	}
}
