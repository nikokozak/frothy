package main

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

func commandProofCtrlC(args []string) error {
	fs := flag.NewFlagSet("proof-ctrlc", flag.ContinueOnError)
	binaryPath := fs.String("binary", "", "Frothy host binary")
	if err := fs.Parse(args); err != nil {
		return err
	}
	return proofCtrlC(resolveFrothyBinary(*binaryPath))
}

func commandProofSafeBoot(args []string) error {
	fs := flag.NewFlagSet("proof-safeboot", flag.ContinueOnError)
	binaryPath := fs.String("binary", "", "Frothy host binary")
	if err := fs.Parse(args); err != nil {
		return err
	}
	return proofSafeBoot(resolveFrothyBinary(*binaryPath))
}

func resolveFrothyBinary(provided string) string {
	if provided != "" {
		return provided
	}
	if fromEnv := os.Getenv("FROTHY_BINARY"); fromEnv != "" {
		return fromEnv
	}
	root, err := detectRepoRoot()
	if err != nil {
		fatalf("error: %v", err)
	}
	return filepath.Join(root, "build", "Frothy")
}

func proofCtrlC(binaryPath string) error {
	if !fileExists(binaryPath) {
		return fmt.Errorf("missing Frothy binary: %s", binaryPath)
	}
	if err := runCtrlCRawByteMultilineCase(binaryPath); err != nil {
		return err
	}
	if err := runCtrlCMultilineCase(binaryPath); err != nil {
		return err
	}
	return runCtrlCLoopCase(binaryPath)
}

func runCtrlCRawByteMultilineCase(binaryPath string) error {
	workDir, err := os.MkdirTemp("", "frothy-m8-ctrl-c.")
	if err != nil {
		return err
	}
	defer os.RemoveAll(workDir)

	session, err := startStreamProcess(binaryPath, workDir)
	if err != nil {
		return err
	}
	defer session.closeWith("quit\n")

	transcript, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	if err := session.send("to inc with x\n"); err != nil {
		return err
	}
	chunk, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyContinue)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("\x03"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("2\n"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt) && bytes.Contains(data, []byte("2\n"))
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)

	text := normalizeNewlines(transcript)
	fmt.Print(text)
	requireOrderedContains(text, frothyPrompt, frothyContinue, "frothy> 2\n", frothyPrompt)
	requireNotContains(text, "parse error (")
	requireNotContains(text, "eval error (")
	return nil
}

func runCtrlCMultilineCase(binaryPath string) error {
	workDir, err := os.MkdirTemp("", "frothy-m8-ctrl-c.")
	if err != nil {
		return err
	}
	defer os.RemoveAll(workDir)

	session, err := startStreamProcess(binaryPath, workDir)
	if err != nil {
		return err
	}
	defer session.closeWith("quit\n")

	transcript, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	if err := session.send("to inc with x\n"); err != nil {
		return err
	}
	chunk, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyContinue)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.interrupt(); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("2\n"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt) && bytes.Contains(data, []byte("2\n"))
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)

	text := normalizeNewlines(transcript)
	fmt.Print(text)
	requireOrderedContains(text, frothyPrompt, frothyContinue, "frothy> 2\n", frothyPrompt)
	requireNotContains(text, "parse error (")
	requireNotContains(text, "eval error (")
	return nil
}

func runCtrlCLoopCase(binaryPath string) error {
	workDir, err := os.MkdirTemp("", "frothy-m8-ctrl-c.")
	if err != nil {
		return err
	}
	defer os.RemoveAll(workDir)

	session, err := startStreamProcess(binaryPath, workDir)
	if err != nil {
		return err
	}
	defer session.closeWith("quit\n")

	transcript, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	if err := session.send("while true [ core: @save ]\n"); err != nil {
		return err
	}
	chunk, err := session.waitFor(func(data []byte) bool {
		return bytes.Contains(data, []byte("<native save/0>"))
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.interrupt(); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("2\n"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return bytes.Contains(data, []byte("2\n")) && hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)

	text := normalizeNewlines(transcript)
	fmt.Print(text)
	requireOrderedContains(text, frothyPrompt, "<native save/0>", "eval error (14)", "frothy> 2\nfrothy> ")
	requireNotContains(text, "parse error (")
	return nil
}

func proofSafeBoot(binaryPath string) error {
	if !fileExists(binaryPath) {
		return fmt.Errorf("missing Frothy binary: %s", binaryPath)
	}
	workDir, err := os.MkdirTemp("", "frothy-m3a-safe-boot.")
	if err != nil {
		return err
	}
	defer os.RemoveAll(workDir)

	setupCmd := exec.Command(binaryPath)
	setupCmd.Dir = workDir
	setupCmd.Stdin = strings.NewReader("note is nil\nto boot [ set note to \"booted\" ]\nsave\nquit\n")
	setupOut, err := setupCmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("setup run failed: %w\n%s", err, string(setupOut))
	}
	setupText := normalizeNewlines(setupOut)
	fmt.Print(setupText)
	requireContains(setupText, "snapshot: none")

	session, err := startStreamProcess(binaryPath, workDir)
	if err != nil {
		return err
	}
	defer session.closeWith("quit\n")

	transcript, err := session.waitFor(func(data []byte) bool {
		return bytes.Contains(data, []byte("boot: CTRL-C for safe boot"))
	}, 3*time.Second)
	if err != nil {
		return err
	}
	if err := session.interrupt(); err != nil {
		return err
	}
	chunk, err := session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("note\n"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)
	if err := session.send("2\n"); err != nil {
		return err
	}
	chunk, err = session.waitFor(func(data []byte) bool {
		return bytes.Contains(data, []byte("2\n")) && hasTerminalPrompt(data, frothyPrompt)
	}, 3*time.Second)
	if err != nil {
		return err
	}
	transcript = append(transcript, chunk...)

	text := normalizeNewlines(transcript)
	fmt.Print(text)
	requireContains(text, "Frothy shell")
	requireOrderedContains(text, "snapshot: found", "boot: CTRL-C for safe boot", "boot: Safe Boot, skipped restore and boot.", "eval error (4)", "frothy> 2\nfrothy> ")
	requireNotContains(text, "\"booted\"")
	requireNotContains(text, "parse error (")
	return nil
}
