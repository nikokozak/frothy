package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func TestFindKernelRootFromPrefersLocalKernel(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "CMakeLists.txt"), []byte("project(Froth)\n"), 0644); err != nil {
		t.Fatalf("write CMakeLists.txt: %v", err)
	}
	if err := os.MkdirAll(filepath.Join(root, "src"), 0755); err != nil {
		t.Fatalf("mkdir src: %v", err)
	}
	if err := os.WriteFile(filepath.Join(root, "src", "froth_vm.h"), []byte("/* vm */\n"), 0644); err != nil {
		t.Fatalf("write froth_vm.h: %v", err)
	}
	start := filepath.Join(root, "nested", "dir")
	if err := os.MkdirAll(start, 0755); err != nil {
		t.Fatalf("mkdir nested dir: %v", err)
	}

	originalEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) {
		return "", fmt.Errorf("sdk fallback should not run")
	}
	defer func() {
		ensureSDKRoot = originalEnsureSDKRoot
	}()

	got, err := findKernelRootFrom(start)
	if err != nil {
		t.Fatalf("findKernelRootFrom: %v", err)
	}
	if got != root {
		t.Fatalf("kernel root = %q, want %q", got, root)
	}
}

func TestFindKernelRootFromFallsBackToSDK(t *testing.T) {
	start := filepath.Join(t.TempDir(), "nested")
	if err := os.MkdirAll(start, 0755); err != nil {
		t.Fatalf("mkdir nested dir: %v", err)
	}

	sdkRoot := filepath.Join(t.TempDir(), "sdk", "frothy-"+frothVersion(t))
	originalEnsureSDKRoot := ensureSDKRoot
	ensureSDKRoot = func() (string, error) {
		return sdkRoot, nil
	}
	defer func() {
		ensureSDKRoot = originalEnsureSDKRoot
	}()

	got, err := findKernelRootFrom(start)
	if err != nil {
		t.Fatalf("findKernelRootFrom: %v", err)
	}
	if got != sdkRoot {
		t.Fatalf("kernel root = %q, want %q", got, sdkRoot)
	}
}
