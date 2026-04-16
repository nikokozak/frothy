package main

import (
	"crypto/sha256"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

func commandEnsureProfile(args []string) error {
	fs := flag.NewFlagSet("ensure-profile", flag.ContinueOnError)
	quiet := fs.Bool("quiet", false, "suppress configure/build banners")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 1 {
		return fmt.Errorf("usage: ensure-profile [--quiet] <name>")
	}
	paths, err := detectPaths()
	if err != nil {
		return err
	}
	return ensureProfile(paths, fs.Arg(0), *quiet)
}

func detectPaths() (pathSet, error) {
	root, err := detectRepoRoot()
	if err != nil {
		return pathSet{}, err
	}
	return pathSet{
		Root:      root,
		BuildRoot: filepath.Join(root, "build", "test"),
		LogRoot:   filepath.Join(root, "build", "test", "logs"),
		GoCache:   filepath.Join(root, ".cache", "go-build"),
	}, nil
}

func detectRepoRoot() (string, error) {
	if root := os.Getenv("FROTH_TEST_REPO_ROOT"); root != "" {
		return root, nil
	}
	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}
	dir := wd
	for {
		if fileExists(filepath.Join(dir, "VERSION")) &&
			fileExists(filepath.Join(dir, "CMakeLists.txt")) &&
			fileExists(filepath.Join(dir, "tools", "cli", "go.mod")) {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	return "", errors.New("could not locate Frothy repo root")
}

func baseTestEnv(paths pathSet) map[string]string {
	runnerBin, _ := os.Executable()
	return map[string]string{
		"GOCACHE":                    paths.GoCache,
		"FROTH_TEST_RUNNER_BIN":      runnerBin,
		"FROTH_TEST_REPO_ROOT":       paths.Root,
		"FROTH_TEST_DEFAULT_PROFILE": "host-default",
		"FROTHY_BINARY":              filepath.Join(profileBuildDir(paths, "host-default"), "Frothy"),
	}
}

func profileBuildDir(paths pathSet, name string) string {
	return filepath.Join(paths.BuildRoot, name)
}

func profileLogPath(paths pathSet, name string) string {
	safe := strings.NewReplacer("/", "_", ":", "_").Replace(name)
	return filepath.Join(paths.LogRoot, safe+".log")
}

func ensureProfile(paths pathSet, name string, quiet bool) error {
	profileDef, ok := profiles[name]
	if !ok {
		return fmt.Errorf("unknown profile: %s", name)
	}

	buildDir := profileBuildDir(paths, name)
	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		return err
	}
	stampPath := filepath.Join(buildDir, ".frothy-test-profile.json")
	cachePath := filepath.Join(buildDir, "CMakeCache.txt")

	args := profileDef.Args(paths.Root)
	inputs, err := profileInputFingerprints(paths.Root, profileDef)
	if err != nil {
		return err
	}
	desired := map[string]any{
		"name": profileDef.Name,
		"args": args,
	}
	if len(inputs) > 0 {
		desired["inputs"] = inputs
	}
	digest := sha256.Sum256([]byte(mustJSON(desired)))
	desired["fingerprint"] = fmt.Sprintf("%x", digest[:])

	needsConfigure := profileNeedsConfigure(cachePath, stampPath, desired)
	if needsConfigure {
		if !quiet {
			fmt.Printf("==> configure profile: %s\n", name)
		}
		cmd := append([]string{"cmake", "-S", paths.Root, "-B", buildDir}, args...)
		if err := runCommand(paths, "profile:"+name+":configure", nil, paths.Root, cmd...); err != nil {
			return err
		}
		if err := os.WriteFile(stampPath, []byte(mustJSONIndented(desired)+"\n"), 0o644); err != nil {
			return err
		}
	}

	if !quiet {
		fmt.Printf("==> build profile: %s\n", name)
	}
	return runCommand(paths, "profile:"+name+":build", nil, paths.Root, "cmake", "--build", buildDir)
}

func profileInputFingerprints(root string, profileDef profile) ([]map[string]string, error) {
	if profileDef.Inputs == nil {
		return nil, nil
	}

	paths := profileDef.Inputs(root)
	out := make([]map[string]string, 0, len(paths))
	for _, path := range paths {
		data, err := os.ReadFile(path)
		if err != nil {
			return nil, fmt.Errorf("read profile input %s: %w", path, err)
		}

		name := path
		if rel, err := filepath.Rel(root, path); err == nil && !strings.HasPrefix(rel, "..") {
			name = filepath.ToSlash(rel)
		}

		sum := sha256.Sum256(data)
		out = append(out, map[string]string{
			"path":   name,
			"sha256": fmt.Sprintf("%x", sum[:]),
		})
	}
	return out, nil
}

func profileNeedsConfigure(cachePath string, stampPath string, desired map[string]any) bool {
	if !fileExists(cachePath) || !fileExists(stampPath) {
		return true
	}

	data, err := os.ReadFile(stampPath)
	if err != nil {
		return true
	}

	var current map[string]any
	if err := json.Unmarshal(data, &current); err != nil {
		return true
	}
	return mustJSON(current) != mustJSON(desired)
}

func runCommand(paths pathSet, name string, extraEnv map[string]string, cwd string, args ...string) error {
	start := time.Now()
	if err := os.MkdirAll(paths.LogRoot, 0o755); err != nil {
		return err
	}
	logPath := profileLogPath(paths, name)
	logFile, err := os.Create(logPath)
	if err != nil {
		return err
	}
	defer logFile.Close()

	cmd := exec.Command(args[0], args[1:]...)
	cmd.Dir = cwd
	cmd.Env = mergeEnv(extraEnv)
	tail := newTailBuffer(64 << 10)
	cmd.Stdout = io.MultiWriter(logFile, tail)
	cmd.Stderr = io.MultiWriter(logFile, tail)

	err = cmd.Run()
	if err != nil {
		if output := tail.Bytes(); len(output) > 0 {
			_, _ = os.Stderr.Write(output)
		}
		return fmt.Errorf("%s failed after %s\nlog: %s", name, time.Since(start).Round(10*time.Millisecond), logPath)
	}
	fmt.Printf("[ok] %s (%s)\n", name, time.Since(start).Round(10*time.Millisecond))
	return nil
}

func sortedProfileNames() []string {
	names := make([]string, 0, len(profiles))
	for name := range profiles {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

func mergeEnv(extra map[string]string) []string {
	envMap := map[string]string{}
	for _, entry := range os.Environ() {
		parts := strings.SplitN(entry, "=", 2)
		if len(parts) == 2 {
			envMap[parts[0]] = parts[1]
		}
	}
	for key, value := range extra {
		envMap[key] = value
	}
	out := make([]string, 0, len(envMap))
	for key, value := range envMap {
		out = append(out, key+"="+value)
	}
	return out
}
