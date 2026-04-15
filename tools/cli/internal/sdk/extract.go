package sdk

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"strings"
)

func VersionFromFS(fsys fs.FS) (string, error) {
	data, err := fs.ReadFile(fsys, payloadVersionName)
	if err != nil {
		return "", fmt.Errorf("read embedded VERSION: %w", err)
	}

	version := strings.TrimSpace(string(data))
	if version == "" {
		return "", fmt.Errorf("parse VERSION from embedded payload")
	}
	return version, nil
}

func FrothyHome() (string, error) {
	if home := os.Getenv("FROTHY_HOME"); home != "" {
		return ensureFrothyHome(home)
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("home dir: %w", err)
	}

	return ensureFrothyHome(filepath.Join(home, ".frothy"))
}

func FrothHome() (string, error) {
	return FrothyHome()
}

func ensureFrothyHome(home string) (string, error) {
	if err := os.MkdirAll(home, 0755); err != nil {
		return "", fmt.Errorf("create Frothy home %s: %w", home, err)
	}
	return home, nil
}

func SDKPath(version string) (string, error) {
	frothHome, err := FrothyHome()
	if err != nil {
		return "", err
	}
	return sdkPathForHome(frothHome, version), nil
}

func EnsureSDK() (string, error) {
	payloadRoot, err := embeddedPayloadFS()
	if err != nil {
		return "", err
	}

	version, err := VersionFromFS(payloadRoot)
	if err != nil {
		return "", err
	}

	frothHome, err := FrothyHome()
	if err != nil {
		return "", err
	}

	return ensureSDKFromFS(payloadRoot, frothHome, version)
}

func ExtractArchive(archive []byte, destDir string) error {
	gzr, err := gzip.NewReader(bytes.NewReader(archive))
	if err != nil {
		return fmt.Errorf("open embedded archive: %w", err)
	}
	defer gzr.Close()

	tr := tar.NewReader(gzr)
	for {
		header, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return fmt.Errorf("read embedded archive: %w", err)
		}

		name := path.Clean(header.Name)
		if name == "." {
			continue
		}
		if path.IsAbs(name) || strings.HasPrefix(name, "../") {
			return fmt.Errorf("reject embedded archive path %q", header.Name)
		}

		destPath := filepath.Join(destDir, filepath.FromSlash(name))
		switch header.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(destPath, 0755); err != nil {
				return fmt.Errorf("create dir %s: %w", destPath, err)
			}
		case tar.TypeReg:
			if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
				return fmt.Errorf("create dir for %s: %w", destPath, err)
			}

			mode := fs.FileMode(0644)
			if perm := fs.FileMode(header.Mode).Perm(); perm != 0 {
				mode = perm
			}

			dst, err := os.OpenFile(destPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
			if err != nil {
				return fmt.Errorf("create %s: %w", destPath, err)
			}
			if _, err := io.Copy(dst, tr); err != nil {
				_ = dst.Close()
				return fmt.Errorf("write %s: %w", destPath, err)
			}
			if err := dst.Close(); err != nil {
				return fmt.Errorf("close %s: %w", destPath, err)
			}
		default:
			return fmt.Errorf("unsupported embedded archive entry %q", header.Name)
		}
	}
}

func embeddedPayloadFS() (fs.FS, error) {
	payloadRoot, err := fs.Sub(PayloadFS, payloadDir)
	if err != nil {
		return nil, fmt.Errorf("open embedded sdk payload: %w", err)
	}
	return payloadRoot, nil
}

func ensureSDKFromFS(fsys fs.FS, frothHome string, version string) (string, error) {
	digest, err := payloadDigestFromFS(fsys)
	if err != nil {
		return "", err
	}
	archive, err := payloadArchiveFromFS(fsys)
	if err != nil {
		return "", err
	}
	if version == "" {
		return "", fmt.Errorf("sdk version is empty")
	}

	destDir := sdkPathForHome(frothHome, version)
	if sdkReady(destDir, version, digest) {
		return destDir, nil
	}

	parentDir := filepath.Dir(destDir)
	if err := os.MkdirAll(parentDir, 0755); err != nil {
		return "", fmt.Errorf("create sdk cache dir: %w", err)
	}

	tempDir, err := os.MkdirTemp(parentDir, ".frothy-sdk-*")
	if err != nil {
		return "", fmt.Errorf("create sdk temp dir: %w", err)
	}

	if err := ExtractArchive(archive, tempDir); err != nil {
		_ = os.RemoveAll(tempDir)
		return "", fmt.Errorf("extract embedded sdk: %w", err)
	}
	if err := writePayloadDigest(tempDir, digest); err != nil {
		_ = os.RemoveAll(tempDir)
		return "", fmt.Errorf("write sdk payload digest: %w", err)
	}

	if err := os.Rename(tempDir, destDir); err != nil {
		if sdkReady(destDir, version, digest) {
			_ = os.RemoveAll(tempDir)
			return destDir, nil
		}

		if removeErr := os.RemoveAll(destDir); removeErr != nil && !os.IsNotExist(removeErr) {
			_ = os.RemoveAll(tempDir)
			return "", fmt.Errorf("clear stale sdk cache: %w", removeErr)
		}
		if retryErr := os.Rename(tempDir, destDir); retryErr != nil {
			if sdkReady(destDir, version, digest) {
				_ = os.RemoveAll(tempDir)
				return destDir, nil
			}
			_ = os.RemoveAll(tempDir)
			return "", fmt.Errorf("activate sdk cache: %w", retryErr)
		}
	}

	return destDir, nil
}

func sdkPathForHome(frothHome string, version string) string {
	return filepath.Join(frothHome, "sdk", "frothy-"+version)
}

func payloadDigestFromFS(fsys fs.FS) (string, error) {
	data, err := fs.ReadFile(fsys, payloadDigestFilename)
	if err != nil {
		return "", fmt.Errorf("read embedded payload digest: %w", err)
	}

	digest := strings.TrimSpace(string(data))
	if digest == "" {
		return "", fmt.Errorf("parse embedded payload digest")
	}
	return digest, nil
}

func payloadArchiveFromFS(fsys fs.FS) ([]byte, error) {
	data, err := fs.ReadFile(fsys, payloadArchiveName)
	if err != nil {
		return nil, fmt.Errorf("read embedded archive: %w", err)
	}
	if len(data) == 0 {
		return nil, fmt.Errorf("embedded archive is empty")
	}
	return data, nil
}

func writePayloadDigest(destDir string, digest string) error {
	return os.WriteFile(filepath.Join(destDir, payloadDigestFilename),
		[]byte(digest+"\n"), 0644)
}

func sdkReady(dir string, version string, digest string) bool {
	return fileMatches(filepath.Join(dir, payloadVersionName), version) &&
		fileMatches(filepath.Join(dir, payloadDigestFilename), digest)
}

func fileMatches(path string, want string) bool {
	data, err := os.ReadFile(path)
	return err == nil && strings.TrimSpace(string(data)) == want
}
