//go:build integration

package cmd

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

var (
	integrationCLIPath string
	integrationCLIHome string
)

func TestMain(m *testing.M) {
	for _, tool := range []string{"cmake", "make", "go"} {
		if _, err := exec.LookPath(tool); err != nil {
			fmt.Fprintf(os.Stderr, "missing required tool for integration tests: %s\n", tool)
			os.Exit(1)
		}
	}

	repoRoot, err := integrationRepoRoot()
	if err != nil {
		fmt.Fprintf(os.Stderr, "determine repo root: %v\n", err)
		os.Exit(1)
	}

	integrationCLIHome, err = os.MkdirTemp("", "froth-cli-integration-")
	if err != nil {
		fmt.Fprintf(os.Stderr, "create temp dir: %v\n", err)
		os.Exit(1)
	}

	integrationCLIPath = filepath.Join(integrationCLIHome, "froth")
	build := exec.Command("go", "build", "-o", integrationCLIPath, ".")
	build.Dir = filepath.Join(repoRoot, "tools", "cli")
	buildOutput, err := build.CombinedOutput()
	if err != nil {
		fmt.Fprintf(os.Stderr, "build integration CLI: %v\n%s", err, buildOutput)
		os.Exit(1)
	}

	code := m.Run()
	_ = os.RemoveAll(integrationCLIHome)
	os.Exit(code)
}

func TestIntegrationNewBuildAndRunBoot(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	if out, err := runIntegrationCLI(t, workspace, env, 2*time.Minute, "new", "hello-posix"); err != nil {
		t.Fatalf("froth new failed: %v\n%s", err, out)
	}

	projectDir := filepath.Join(workspace, "hello-posix")
	if out, err := runIntegrationCLI(t, projectDir, env, 3*time.Minute, "build"); err != nil {
		t.Fatalf("froth build failed: %v\n%s", err, out)
	}

	binaryPath := filepath.Join(projectDir, ".froth-build", "firmware", "Frothy")
	output := runBinaryAfterBoot(t, binaryPath, projectDir, "note\n", 20*time.Second)
	assertContains(t, output, `"Hello from Frothy!"`)
}

func TestIntegrationMultiFileProjectBuildsAndResolvesDependencies(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	if out, err := runIntegrationCLI(t, workspace, env, 2*time.Minute, "new", "multi-file"); err != nil {
		t.Fatalf("froth new failed: %v\n%s", err, out)
	}

	projectDir := filepath.Join(workspace, "multi-file")
	mustWriteIntegrationFile(t, filepath.Join(projectDir, "froth.toml"), `[project]
name = "multi-file"
version = "0.1.0"
entry = "src/main.froth"

[target]
board = "posix"
platform = "posix"

[dependencies]
helpers = { path = "lib/helpers.froth" }
`)
	mustWriteIntegrationFile(t, filepath.Join(projectDir, "lib", "helpers.froth"), `helper() = 41
`)
	mustWriteIntegrationFile(t, filepath.Join(projectDir, "src", "extra.froth"), `extra() = 1
`)
	mustWriteIntegrationFile(t, filepath.Join(projectDir, "src", "main.froth"), `\ #use "helpers"
\ #use "./extra.froth"

total = nil

boot {
  set total = helper() + extra()
}
`)

	if out, err := runIntegrationCLI(t, projectDir, env, 3*time.Minute, "build"); err != nil {
		t.Fatalf("froth build failed: %v\n%s", err, out)
	}

	binaryPath := filepath.Join(projectDir, ".froth-build", "firmware", "Frothy")
	output := runBinaryAfterBoot(t, binaryPath, projectDir, "total\n", 20*time.Second)
	assertContains(t, output, "42")
}

func TestIntegrationConnectLocalBuildsAndLaunchesBinary(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	output := runProcessAfterReady(t, workspace, env, 3*time.Minute, "frothy> ", "quit\n", integrationCLIPath, "connect", "--local")
	assertContains(t, output, "Connected to posix on local")
	assertContains(t, output, "frothy>")

	home := integrationHomeFromEnv(t, env)
	localBinary := filepath.Join(home, "frothy-local-build", "Frothy")
	if _, err := os.Stat(localBinary); err != nil {
		t.Fatalf("local binary missing after connect --local: %v", err)
	}
}

func TestIntegrationBuildCleanRemovesArtifacts(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	if out, err := runIntegrationCLI(t, workspace, env, 2*time.Minute, "new", "clean-build"); err != nil {
		t.Fatalf("froth new failed: %v\n%s", err, out)
	}

	projectDir := filepath.Join(workspace, "clean-build")
	if out, err := runIntegrationCLI(t, projectDir, env, 3*time.Minute, "build"); err != nil {
		t.Fatalf("initial build failed: %v\n%s", err, out)
	}

	marker := filepath.Join(projectDir, ".froth-build", "marker.txt")
	mustWriteIntegrationFile(t, marker, "remove me")

	out, err := runIntegrationCLI(t, projectDir, env, 3*time.Minute, "build", "--clean")
	if err != nil {
		t.Fatalf("clean build failed: %v\n%s", err, out)
	}
	assertContains(t, out, "Cleaned build directory")

	if _, err := os.Stat(marker); !os.IsNotExist(err) {
		t.Fatalf("marker still exists after --clean: %v", err)
	}

	binaryPath := filepath.Join(projectDir, ".froth-build", "firmware", "Frothy")
	if _, err := os.Stat(binaryPath); err != nil {
		t.Fatalf("firmware missing after clean build: %v", err)
	}
}

func TestIntegrationDoctorShowsProjectInfo(t *testing.T) {
	env := integrationEnv(t)
	workspace := t.TempDir()

	if out, err := runIntegrationCLI(t, workspace, env, 2*time.Minute, "new", "doctor-project"); err != nil {
		t.Fatalf("froth new failed: %v\n%s", err, out)
	}

	projectDir := filepath.Join(workspace, "doctor-project")
	out, err := runIntegrationCLI(t, projectDir, env, 90*time.Second, "doctor")
	if err != nil {
		t.Fatalf("froth doctor failed: %v\n%s", err, out)
	}

	assertContains(t, out, "project: doctor-project")
	assertContains(t, out, "target: posix (posix)")
	assertContains(t, out, "entry: src/main.froth")
}

func integrationRepoRoot() (string, error) {
	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}
	return filepath.Clean(filepath.Join(wd, "..", "..", "..")), nil
}

func integrationEnv(t *testing.T) []string {
	t.Helper()

	home := filepath.Join(t.TempDir(), "froth-home")
	if err := os.MkdirAll(home, 0755); err != nil {
		t.Fatalf("mkdir froth home: %v", err)
	}

	env := os.Environ()
	env = append(env, "FROTH_HOME="+home)
	return env
}

func integrationHomeFromEnv(t *testing.T, env []string) string {
	t.Helper()

	for _, entry := range env {
		if strings.HasPrefix(entry, "FROTH_HOME=") {
			return strings.TrimPrefix(entry, "FROTH_HOME=")
		}
	}
	t.Fatal("FROTH_HOME missing from environment")
	return ""
}

func runIntegrationCLI(t *testing.T, dir string, env []string, timeout time.Duration, args ...string) (string, error) {
	t.Helper()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, integrationCLIPath, args...)
	cmd.Dir = dir
	cmd.Env = env

	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out

	err := cmd.Run()
	if ctx.Err() != nil {
		return out.String(), ctx.Err()
	}
	return out.String(), err
}

func runBinaryAfterBoot(t *testing.T, binaryPath string, dir string, source string, timeout time.Duration) string {
	t.Helper()
	return runProcessAfterReady(t, dir, os.Environ(), timeout, "boot: CTRL-C for safe boot", source, binaryPath)
}

func runProcessAfterBoot(t *testing.T, dir string, env []string, timeout time.Duration, source string, command string, args ...string) string {
	t.Helper()
	return runProcessAfterReady(t, dir, env, timeout, "boot: CTRL-C for safe boot", source, command, args...)
}

func runProcessAfterReady(t *testing.T, dir string, env []string, timeout time.Duration, readyNeedle string, source string, command string, args ...string) string {
	t.Helper()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, command, args...)
	cmd.Dir = dir
	cmd.Env = env

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatalf("stdout pipe: %v", err)
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		t.Fatalf("stderr pipe: %v", err)
	}
	stdin, err := cmd.StdinPipe()
	if err != nil {
		t.Fatalf("stdin pipe: %v", err)
	}

	buf := &watchedBuffer{
		needle: readyNeedle,
		seen:   make(chan struct{}),
	}

	if err := cmd.Start(); err != nil {
		t.Fatalf("start %s: %v", command, err)
	}

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		_, _ = io.Copy(buf, stdout)
	}()
	go func() {
		defer wg.Done()
		_, _ = io.Copy(buf, stderr)
	}()

	select {
	case <-buf.seen:
	case <-ctx.Done():
		_ = stdin.Close()
		_ = cmd.Wait()
		t.Fatalf("waiting for ready output %q: %v\n%s", readyNeedle, ctx.Err(), buf.String())
	}

	time.Sleep(1100 * time.Millisecond)
	if source != "" {
		if _, err := io.WriteString(stdin, source); err != nil {
			t.Fatalf("write source: %v", err)
		}
		if !strings.HasSuffix(source, "\n") {
			if _, err := io.WriteString(stdin, "\n"); err != nil {
				t.Fatalf("write newline: %v", err)
			}
		}
	}
	if _, err := io.WriteString(stdin, "\x04"); err != nil {
		t.Fatalf("write eot: %v", err)
	}
	_ = stdin.Close()

	err = cmd.Wait()
	wg.Wait()

	if ctx.Err() != nil {
		t.Fatalf("process timeout: %v\n%s", ctx.Err(), buf.String())
	}
	if err != nil {
		t.Fatalf("%s failed: %v\n%s", command, err, buf.String())
	}

	return buf.String()
}

type watchedBuffer struct {
	mu     sync.Mutex
	buf    bytes.Buffer
	needle string
	seen   chan struct{}
	once   sync.Once
}

func (w *watchedBuffer) Write(p []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	n, err := w.buf.Write(p)
	if strings.Contains(w.buf.String(), w.needle) {
		w.once.Do(func() {
			close(w.seen)
		})
	}
	return n, err
}

func (w *watchedBuffer) String() string {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.buf.String()
}

func mustWriteIntegrationFile(t *testing.T, path string, content string) {
	t.Helper()

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

func assertContains(t *testing.T, output string, substring string) {
	t.Helper()

	if !strings.Contains(output, substring) {
		t.Fatalf("expected %q in output:\n%s", substring, output)
	}
}
