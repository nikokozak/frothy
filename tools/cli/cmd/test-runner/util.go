package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

type tailBuffer struct {
	data []byte
	keep int
}

func newTailBuffer(keep int) *tailBuffer {
	return &tailBuffer{keep: keep}
}

func (b *tailBuffer) Write(p []byte) (int, error) {
	if len(p) >= b.keep {
		b.data = append(b.data[:0], p[len(p)-b.keep:]...)
		return len(p), nil
	}
	if len(b.data)+len(p) > b.keep {
		drop := len(b.data) + len(p) - b.keep
		b.data = append(b.data[:0], b.data[drop:]...)
	}
	b.data = append(b.data, p...)
	return len(p), nil
}

func (b *tailBuffer) Bytes() []byte {
	return append([]byte(nil), b.data...)
}

func normalizeNewlines(data []byte) string {
	return strings.ReplaceAll(string(data), "\r\n", "\n")
}

func requireContains(text string, needle string) {
	if !strings.Contains(text, needle) {
		fatalf("error: expected output to contain: %s", needle)
	}
}

func requireNotContains(text string, needle string) {
	if strings.Contains(text, needle) {
		fatalf("error: output unexpectedly contained: %s", needle)
	}
}

func requireOrderedContains(text string, needles ...string) {
	start := 0
	for _, needle := range needles {
		offset := strings.Index(text[start:], needle)
		if offset < 0 {
			fatalf("error: expected output to contain in order: %s", needle)
		}
		start += offset + len(needle)
	}
}

func countPromptOccurrences(data []byte, prompt string) int {
	promptBytes := []byte(prompt)
	count := 0
	start := 0
	for {
		index := bytes.Index(data[start:], promptBytes)
		if index < 0 {
			return count
		}
		index += start
		if index == 0 || data[index-1] == '\n' {
			count++
		}
		start = index + len(promptBytes)
	}
}

func hasTerminalPrompt(data []byte, prompt string) bool {
	promptBytes := []byte(prompt)
	if len(data) < len(promptBytes) || !bytes.HasSuffix(data, promptBytes) {
		return false
	}
	index := len(data) - len(promptBytes)
	return index == 0 || data[index-1] == '\n'
}

func mustJSON(value any) string {
	data, err := json.Marshal(value)
	if err != nil {
		panic(err)
	}
	return string(data)
}

func mustJSONIndented(value any) string {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		panic(err)
	}
	return string(data)
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
