package project

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

// ResolvedFFIConfig holds validated, absolute paths for project FFI.
type ResolvedFFIConfig struct {
	Sources  []string
	Includes []string
	Defines  []string
}

var cIdentifier = regexp.MustCompile(`^[a-zA-Z_][a-zA-Z0-9_]*$`)

func ResolveFFI(manifest *Manifest, projectRoot string) (*ResolvedFFIConfig, error) {
	if manifest == nil {
		return nil, fmt.Errorf("manifest is nil")
	}

	hasSources := len(manifest.FFI.Sources) > 0
	hasIncludes := len(manifest.FFI.Includes) > 0
	hasDefines := len(manifest.FFI.Defines) > 0

	if !hasSources && (hasIncludes || hasDefines) {
		return nil, fmt.Errorf("[ffi] includes or defines without sources")
	}
	if !hasSources {
		return nil, nil
	}

	absRoot, err := filepath.EvalSymlinks(projectRoot)
	if err != nil {
		return nil, fmt.Errorf("resolve project root: %w", err)
	}

	var resolved ResolvedFFIConfig

	// Sources: must exist, must be .c, must stay under project root.
	for _, source := range manifest.FFI.Sources {
		absPath := filepath.Join(absRoot, source)
		realPath, err := filepath.EvalSymlinks(absPath)
		if err != nil {
			return nil, fmt.Errorf("[ffi] source %q: %w", source, err)
		}
		if !strings.HasPrefix(realPath, absRoot+string(filepath.Separator)) {
			return nil, fmt.Errorf("[ffi] source %q escapes project root", source)
		}
		info, err := os.Stat(realPath)
		if err != nil {
			return nil, fmt.Errorf("[ffi] source %q: %w", source, err)
		}
		if info.IsDir() {
			return nil, fmt.Errorf("[ffi] source %q is a directory, expected a .c file", source)
		}
		if filepath.Ext(realPath) != ".c" {
			return nil, fmt.Errorf("[ffi] source %q is not a .c file", source)
		}
		resolved.Sources = append(resolved.Sources, realPath)
	}

	// Includes: must exist, must be a directory, must stay under project root.
	for _, inc := range manifest.FFI.Includes {
		absPath := filepath.Join(absRoot, inc)
		realPath, err := filepath.EvalSymlinks(absPath)
		if err != nil {
			return nil, fmt.Errorf("[ffi] include %q: %w", inc, err)
		}
		if !strings.HasPrefix(realPath, absRoot+string(filepath.Separator)) {
			return nil, fmt.Errorf("[ffi] include %q escapes project root", inc)
		}
		info, err := os.Stat(realPath)
		if err != nil {
			return nil, fmt.Errorf("[ffi] include %q: %w", inc, err)
		}
		if !info.IsDir() {
			return nil, fmt.Errorf("[ffi] include %q is not a directory", inc)
		}
		resolved.Includes = append(resolved.Includes, realPath)
	}

	// Defines: key must be a valid C identifier, value must not contain
	// newlines, semicolons, or quotes.
	defineKeys := make([]string, 0, len(manifest.FFI.Defines))
	for key := range manifest.FFI.Defines {
		defineKeys = append(defineKeys, key)
	}
	sort.Strings(defineKeys)
	for _, key := range defineKeys {
		value := manifest.FFI.Defines[key]
		if !cIdentifier.MatchString(key) {
			return nil, fmt.Errorf("[ffi] define key %q is not a valid C identifier", key)
		}
		if strings.ContainsAny(value, "\n;\"'") {
			return nil, fmt.Errorf("[ffi] define %q has invalid characters in value", key)
		}
		resolved.Defines = append(resolved.Defines, fmt.Sprintf("%s=%s", key, value))
	}

	return &resolved, nil
}
