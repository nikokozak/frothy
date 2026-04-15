package cmd

import (
	"strings"
	"testing"
)

func TestResolveLegacyCLISelectionInfersPlatformFromBoard(t *testing.T) {
	resetCommandGlobals(t)
	boardFlag = "esp32-devkit-v4-game-board"

	root := makeFakeKernelRoot(t)
	selection, err := resolveLegacyCLISelection(root)
	if err != nil {
		t.Fatalf("resolveLegacyCLISelection: %v", err)
	}

	if selection.Platform != "esp-idf" {
		t.Fatalf("platform = %q, want esp-idf", selection.Platform)
	}
	if selection.Board != "esp32-devkit-v4-game-board" {
		t.Fatalf("board = %q, want v4 board", selection.Board)
	}
}

func TestResolveLegacyCLISelectionRejectsBoardPassedAsTarget(t *testing.T) {
	resetCommandGlobals(t)
	targetFlag = "esp32-devkit-v4-game-board"

	root := makeFakeKernelRoot(t)
	_, err := resolveLegacyCLISelection(root)
	if err == nil {
		t.Fatal("resolveLegacyCLISelection succeeded, want error")
	}
	if !strings.Contains(err.Error(), "use `--board esp32-devkit-v4-game-board` instead") {
		t.Fatalf("error = %v, want board guidance", err)
	}
}
