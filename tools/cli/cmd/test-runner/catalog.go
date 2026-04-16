package main

import (
	"io"
	"os/exec"
	"path/filepath"
	"sync"
)

const (
	legacyPrompt       = "froth> "
	frothyPrompt       = "frothy> "
	frothyContinue     = ".. "
	snapshotHeaderSize = 50
	payloadCRC32Offset = 26
	headerCRC32Offset  = 30
	frothCallTag       = 6
)

type profile struct {
	Name        string
	Description string
	Args        func(root string) []string
	Inputs      func(root string) []string
}

type pathSet struct {
	Root      string
	BuildRoot string
	LogRoot   string
	GoCache   string
}

type streamProcess struct {
	cmd        *exec.Cmd
	stdin      io.WriteCloser
	waitDone   chan struct{}
	waitErr    error
	pending    []byte
	outputMu   sync.Mutex
	outputCond *sync.Cond
	output     []byte
	closed     bool
}

var profiles = map[string]profile{
	"host-default": {
		Name:        "host-default",
		Description: "Default host Frothy test build.",
		Args: func(string) []string {
			return []string{
				"-U", "FROTH_*",
				"-U", "FROTHY_*",
				"-DFROTH_CELL_SIZE_BITS=32",
				"-DFROTHY_BUILD_HOST=ON",
			}
		},
		Inputs: func(root string) []string {
			return []string{
				filepath.Join(root, "CMakeLists.txt"),
				filepath.Join(root, "cmake", "froth_board_assets.cmake"),
				filepath.Join(root, "boards", "posix", "board.json"),
			}
		},
	},
}
