package cmd

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

var (
	setupHTTPClient          = &http.Client{Timeout: 30 * time.Second}
	rawContentBase           = "https://raw.githubusercontent.com"
	releaseDownloadBaseURLFn = releaseDownloadBaseURL
)

func runSetup(args []string) error {
	if len(args) == 0 {
		fmt.Println("Usage: froth setup <target>")
		fmt.Println()
		fmt.Println("Available setup targets:")
		fmt.Println("  esp-idf")
		return nil
	}

	switch args[0] {
	case "esp-idf":
		return runSetupESPIDF(args[1:])
	default:
		return fmt.Errorf("unknown setup target: %s", args[0])
	}
}

func runSetupESPIDF(args []string) error {
	var scriptArgs []string
	for _, arg := range args {
		switch arg {
		case "--force":
			scriptArgs = append(scriptArgs, arg)
		default:
			return fmt.Errorf("unknown argument for `froth setup esp-idf`: %s", arg)
		}
	}

	if kernelRoot, err := findLocalKernelRoot(); err == nil {
		scriptPath := filepath.Join(kernelRoot, "tools", "setup-esp-idf.sh")
		if _, err := os.Stat(scriptPath); err == nil {
			return runSetupScript(scriptPath, scriptArgs)
		}
	}

	version, err := cliVersion()
	if err != nil {
		return err
	}

	scriptURL := rawTaggedURL(version, "tools/setup-esp-idf.sh")
	tempDir, err := os.MkdirTemp("", "froth-setup-esp-idf-*")
	if err != nil {
		return fmt.Errorf("create temp dir: %w", err)
	}
	defer os.RemoveAll(tempDir)

	scriptPath := filepath.Join(tempDir, "setup-esp-idf.sh")
	if err := downloadFile(scriptURL, scriptPath); err != nil {
		return fmt.Errorf("download ESP-IDF setup script: %w", err)
	}
	if err := os.Chmod(scriptPath, 0755); err != nil {
		return fmt.Errorf("chmod setup script: %w", err)
	}

	return runSetupScript(scriptPath, scriptArgs)
}

func runSetupScript(scriptPath string, scriptArgs []string) error {
	cmdArgs := append([]string{scriptPath}, scriptArgs...)
	cmd := exec.Command("bash", cmdArgs...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.Stdin = os.Stdin
	cmd.Env = os.Environ()
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("run setup script: %w", err)
	}
	return nil
}

func downloadFile(url string, destPath string) error {
	resp, err := setupHTTPClient.Get(url)
	if err != nil {
		return fmt.Errorf("GET %s: %w", url, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("GET %s: unexpected status %s", url, resp.Status)
	}

	if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
		return fmt.Errorf("create download dir: %w", err)
	}

	file, err := os.OpenFile(destPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("create %s: %w", destPath, err)
	}
	defer file.Close()

	if _, err := io.Copy(file, resp.Body); err != nil {
		return fmt.Errorf("write %s: %w", destPath, err)
	}

	return nil
}

func releaseAssetURL(version string, asset string) string {
	base := strings.TrimRight(releaseDownloadBaseURLFn(version), "/")
	return fmt.Sprintf("%s/%s", base, asset)
}

func rawTaggedURL(version string, path string) string {
	base := strings.TrimRight(rawContentBase, "/")
	cleanPath := strings.TrimLeft(path, "/")
	return fmt.Sprintf("%s/%s/v%s/%s", base, releaseRepoSlug(), version, cleanPath)
}

func releaseRepoSlug() string {
	if slug := strings.TrimSpace(os.Getenv("RELEASE_REPO_SLUG")); slug != "" {
		return slug
	}
	return releaseDefault("FROTHY_DEFAULT_RELEASE_REPO_SLUG")
}

func releaseDownloadBaseURL(version string) string {
	return "https://github.com/" + releaseRepoSlug() + "/releases/download/v" +
		version
}
