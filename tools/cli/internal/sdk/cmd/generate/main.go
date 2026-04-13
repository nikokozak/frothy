package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

var (
	repoRoot = flag.String("repo", "", "Frothy repo root")
	outDir   = flag.String("out", "", "output directory for generated payload")
)

var includePaths = []string{
	"VERSION",
	"CMakeLists.txt",
	"cmake",
	"src",
	"boards",
	"platforms",
	"targets/esp-idf",
}

var ignoredBaseNames = map[string]struct{}{
	".DS_Store":         {},
	"sdkconfig":         {},
	"sdkconfig.old":     {},
	"dependencies.lock": {},
}

var ignoredDirNames = map[string]struct{}{
	".cache":             {},
	"__pycache__":        {},
	"build":              {},
	"managed_components": {},
}

type payloadFile struct {
	relPath string
	mode    fs.FileMode
	data    []byte
}

func main() {
	flag.Parse()

	if *repoRoot == "" || *outDir == "" {
		flag.Usage()
		os.Exit(2)
	}

	versionData, err := os.ReadFile(filepath.Join(*repoRoot, "VERSION"))
	if err != nil {
		fatalf("read VERSION: %v", err)
	}
	version := strings.TrimSpace(string(versionData))
	if version == "" {
		fatalf("VERSION is empty")
	}

	files, err := collectPayloadFiles(*repoRoot)
	if err != nil {
		fatalf("collect payload files: %v", err)
	}

	archive, err := buildArchive(files)
	if err != nil {
		fatalf("build archive: %v", err)
	}

	digest := sha256.Sum256(archive)
	if err := os.MkdirAll(*outDir, 0755); err != nil {
		fatalf("create output dir: %v", err)
	}
	if err := os.WriteFile(filepath.Join(*outDir, "kernel.tar.gz"), archive, 0644); err != nil {
		fatalf("write archive: %v", err)
	}
	if err := os.WriteFile(filepath.Join(*outDir, "VERSION"), []byte(version+"\n"), 0644); err != nil {
		fatalf("write VERSION: %v", err)
	}
	if err := os.WriteFile(
		filepath.Join(*outDir, "PAYLOAD_SHA256"),
		[]byte(hex.EncodeToString(digest[:])+"\n"),
		0644,
	); err != nil {
		fatalf("write payload digest: %v", err)
	}
}

func collectPayloadFiles(root string) ([]payloadFile, error) {
	var files []payloadFile

	for _, includePath := range includePaths {
		fullPath := filepath.Join(root, includePath)
		info, err := os.Stat(fullPath)
		if err != nil {
			return nil, err
		}

		if info.IsDir() {
			err = filepath.WalkDir(fullPath, func(path string, d fs.DirEntry, walkErr error) error {
				if walkErr != nil {
					return walkErr
				}
				if path == fullPath {
					return nil
				}
				if shouldSkipPath(d) {
					if d.IsDir() {
						return filepath.SkipDir
					}
					return nil
				}
				if d.IsDir() {
					return nil
				}
				file, err := readPayloadFile(root, path)
				if err != nil {
					return err
				}
				files = append(files, file)
				return nil
			})
			if err != nil {
				return nil, err
			}
			continue
		}

		file, err := readPayloadFile(root, fullPath)
		if err != nil {
			return nil, err
		}
		files = append(files, file)
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].relPath < files[j].relPath
	})
	return files, nil
}

func shouldSkipPath(entry fs.DirEntry) bool {
	name := entry.Name()
	if _, ok := ignoredBaseNames[name]; ok {
		return true
	}
	if entry.IsDir() {
		_, ok := ignoredDirNames[name]
		return ok
	}
	return false
}

func readPayloadFile(root string, fullPath string) (payloadFile, error) {
	data, err := os.ReadFile(fullPath)
	if err != nil {
		return payloadFile{}, err
	}
	info, err := os.Stat(fullPath)
	if err != nil {
		return payloadFile{}, err
	}

	relPath, err := filepath.Rel(root, fullPath)
	if err != nil {
		return payloadFile{}, err
	}

	return payloadFile{
		relPath: filepath.ToSlash(relPath),
		mode:    info.Mode().Perm(),
		data:    data,
	}, nil
}

func buildArchive(files []payloadFile) ([]byte, error) {
	var buf bytes.Buffer

	gzw, err := gzip.NewWriterLevel(&buf, gzip.BestCompression)
	if err != nil {
		return nil, err
	}
	gzw.ModTime = time.Unix(0, 0)

	tw := tar.NewWriter(gzw)
	for _, file := range files {
		header := &tar.Header{
			Name:    file.relPath,
			Mode:    int64(file.mode),
			Size:    int64(len(file.data)),
			ModTime: time.Unix(0, 0),
		}
		if err := tw.WriteHeader(header); err != nil {
			_ = tw.Close()
			_ = gzw.Close()
			return nil, err
		}
		if _, err := io.Copy(tw, bytes.NewReader(file.data)); err != nil {
			_ = tw.Close()
			_ = gzw.Close()
			return nil, err
		}
	}

	if err := tw.Close(); err != nil {
		_ = gzw.Close()
		return nil, err
	}
	if err := gzw.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", args...)
	os.Exit(1)
}
