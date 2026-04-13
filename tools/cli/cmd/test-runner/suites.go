package main

import (
	"fmt"
	"path/filepath"
)

func runFast() error {
	if err := runFrothy(); err != nil {
		return err
	}
	return runCLIUnit()
}

func runAll() error {
	if err := runFast(); err != nil {
		return err
	}
	if err := runCLILocal(); err != nil {
		return err
	}
	return runCLIIntegration()
}

func runFrothy() error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	if err := ensureProfile(paths, "host-default", false); err != nil {
		return err
	}
	env := baseTestEnv(paths)
	buildDir := profileBuildDir(paths, "host-default")
	if err := runCommand(paths, "frothy:ctest", env, paths.Root, "ctest", "--test-dir", buildDir, "--output-on-failure", "-L", "frothy"); err != nil {
		return err
	}
	return runCommand(paths, "frothy:proofs", env, paths.Root, "sh", filepath.Join(paths.Root, "tools", "frothy", "proof.sh"), "host")
}

func runCLIUnit() error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	return runCommand(paths, "cli:unit", baseTestEnv(paths), paths.Root, "make", "--no-print-directory", "-C", filepath.Join(paths.Root, "tools", "cli"), "test-unit")
}

func runCLILocal() error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	if err := ensureProfile(paths, "host-default", false); err != nil {
		return err
	}
	env := baseTestEnv(paths)
	env["FROTH_TEST_LOCAL_RUNTIME"] = filepath.Join(profileBuildDir(paths, "host-default"), "Frothy")
	return runCommand(paths, "cli:localruntime", env, paths.Root, "make", "--no-print-directory", "-C", filepath.Join(paths.Root, "tools", "cli"), "test-localruntime")
}

func runCLIIntegration() error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	return runCommand(paths, "cli:integration", baseTestEnv(paths), paths.Root, "make", "--no-print-directory", "-C", filepath.Join(paths.Root, "tools", "cli"), "test-integration")
}

func printList() {
	fmt.Println("fast")
	fmt.Println("  frothy")
	fmt.Println("  cli")
	fmt.Println("all")
	fmt.Println("  fast")
	fmt.Println("  cli-local")
	fmt.Println("  integration")
	fmt.Println("profiles")
	for _, name := range sortedProfileNames() {
		fmt.Printf("  %s: %s\n", name, profiles[name].Description)
	}
}
