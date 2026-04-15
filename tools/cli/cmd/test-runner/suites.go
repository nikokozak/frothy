package main

import (
	"fmt"
	"os"
	"path/filepath"
)

func ensureCLI(paths pathSet, env map[string]string, stepName string) error {
	return runCommand(
		paths,
		stepName,
		env,
		paths.Root,
		"make",
		"--no-print-directory",
		"-C",
		filepath.Join(paths.Root, "tools", "cli"),
		"build",
	)
}

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
	if err := ensureCLI(paths, env, "frothy:cli-build"); err != nil {
		return err
	}
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

func runVSCode() error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	if err := ensureProfile(paths, "host-default", false); err != nil {
		return err
	}
	env := baseTestEnv(paths)
	if err := prepareVSCode(paths, env); err != nil {
		return err
	}
	return runCommand(
		paths,
		"vscode:proof-host",
		env,
		paths.Root,
		"sh",
		filepath.Join(paths.Root, "tools", "frothy", "proof_editor_smoke.sh"),
		"--host-only",
	)
}

func runVSCodeBoard(port string) error {
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	if err := ensureProfile(paths, "host-default", false); err != nil {
		return err
	}
	env := baseTestEnv(paths)
	if err := prepareVSCodeBoard(paths, env); err != nil {
		return err
	}
	return runCommand(
		paths,
		"vscode:proof-board",
		env,
		paths.Root,
		"sh",
		filepath.Join(paths.Root, "tools", "frothy", "proof_editor_smoke.sh"),
		port,
	)
}

func prepareVSCode(paths pathSet, env map[string]string) error {
	if err := ensureCLI(paths, env, "vscode:cli-build"); err != nil {
		return err
	}
	if err := runCommand(
		paths,
		"vscode:npm-ci",
		env,
		filepath.Join(paths.Root, "tools", "vscode"),
		"npm",
		"ci",
	); err != nil {
		return err
	}
	if err := runCommand(
		paths,
		"vscode:test",
		env,
		filepath.Join(paths.Root, "tools", "vscode"),
		"npm",
		"test",
	); err != nil {
		return err
	}
	if err := runCommand(
		paths,
		"vscode:package-smoke",
		env,
		filepath.Join(paths.Root, "tools", "vscode"),
		"npm",
		"run",
		"test:package",
	); err != nil {
		return err
	}
	return nil
}

func prepareVSCodeBoard(paths pathSet, env map[string]string) error {
	if err := ensureCLI(paths, env, "vscode:cli-build"); err != nil {
		return err
	}
	vscodeDir := filepath.Join(paths.Root, "tools", "vscode")
	if _, err := os.Stat(filepath.Join(vscodeDir, "node_modules")); err != nil {
		if !os.IsNotExist(err) {
			return err
		}
		if err := runCommand(
			paths,
			"vscode:npm-ci",
			env,
			vscodeDir,
			"npm",
			"ci",
		); err != nil {
			return err
		}
	}

	return runCommand(
		paths,
		"vscode:compile",
		env,
		vscodeDir,
		"npm",
		"run",
		"compile",
	)
}

func boardSmokePort() string {
	return os.Getenv("FROTHY_EDITOR_SMOKE_PORT")
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
	fmt.Println("  core local gate (C, Go, shell)")
	fmt.Println("  includes: frothy, cli")
	fmt.Println("all")
	fmt.Println("  extended local gate (C, Go, shell)")
	fmt.Println("  includes: fast, cli-local, integration")
	fmt.Println("frothy")
	fmt.Println("  host ctest + core proof.sh host lane")
	fmt.Println("cli")
	fmt.Println("  CLI unit tests")
	fmt.Println("cli-local")
	fmt.Println("  CLI local-runtime tests")
	fmt.Println("integration")
	fmt.Println("  CLI integration tests")
	fmt.Println("vscode")
	fmt.Println("  extension-local Node lane: npm test, npm run test:package, host editor smoke")
	fmt.Println("vscode-board --port <PORT>")
	fmt.Println("  extension board smoke on a real device")
	fmt.Println("profiles")
	for _, name := range sortedProfileNames() {
		fmt.Printf("  %s: %s\n", name, profiles[name].Description)
	}
}
