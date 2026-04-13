package project

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/BurntSushi/toml"
)

const ManifestFile = "froth.toml"

// Manifest is the parsed contents of a froth.toml file.
type Manifest struct {
	Project      ProjectConfig               `toml:"project"`
	Target       TargetConfig                `toml:"target"`
	Build        BuildConfig                 `toml:"build"`
	FFI          FFIConfig                   `toml:"ffi"`
	Platform     map[string]PlatformConfig   `toml:"platform"`
	Dependencies map[string]DependencyConfig `toml:"dependencies"`
}

type ProjectConfig struct {
	Name    string `toml:"name"`
	Version string `toml:"version"`
	Entry   string `toml:"entry"`
	Froth   string `toml:"froth"`
}

type TargetConfig struct {
	Board    string `toml:"board"`
	Platform string `toml:"platform"`
}

type BuildConfig struct {
	CellSize       *int `toml:"cell_size"`
	HeapSize       *int `toml:"heap_size"`
	SlotTableSize  *int `toml:"slot_table_size"`
	LineBufferSize *int `toml:"line_buffer_size"`
	TbufSize       *int `toml:"tbuf_size"`
	TdescMax       *int `toml:"tdesc_max"`
	FFIMaxTables   *int `toml:"ffi_max_tables"`
}

type FFIConfig struct {
	Sources  []string          `toml:"sources"`
	Includes []string          `toml:"includes"`
	Defines  map[string]string `toml:"defines"`
}

// PlatformConfig uses a map to accept arbitrary platform-specific keys
// without triggering Undecoded() warnings. Known keys are accessed
// via helper methods; unknown keys are passed through to the build system.
type PlatformConfig map[string]interface{}

func (p PlatformConfig) String(key string) string {
	if v, ok := p[key]; ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return ""
}

type DependencyConfig struct {
	Path string `toml:"path"`
	// Future: Git string `toml:"git"`
	// Future: Tag string `toml:"tag"`
}

// LoadManifest reads and parses a froth.toml file at the given path.
func LoadManifest(path string) (*Manifest, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read manifest: %w", err)
	}

	var m Manifest
	md, err := toml.Decode(string(data), &m)
	if err != nil {
		return nil, fmt.Errorf("parse manifest: %w", err)
	}

	// Warn on unknown keys (catches typos like [bilud] or cell_siz)
	if undecoded := md.Undecoded(); len(undecoded) > 0 {
		for _, key := range undecoded {
			fmt.Fprintf(os.Stderr, "warning: unknown key in %s: %s\n", ManifestFile, key)
		}
	}

	// Apply defaults
	if m.Project.Version == "" {
		m.Project.Version = "0.0.1"
	}
	if m.Project.Entry == "" {
		m.Project.Entry = "src/main.froth"
	}
	if m.Target.Board == "" {
		m.Target.Board = "posix"
	}
	if m.Target.Platform == "" {
		m.Target.Platform = "posix"
	}

	return &m, nil
}

// FindProjectRoot walks up from startDir looking for a froth.toml file.
// Returns the directory containing it, or an error if not found.
func FindProjectRoot(startDir string) (string, error) {
	dir, err := filepath.Abs(startDir)
	if err != nil {
		return "", fmt.Errorf("abs path: %w", err)
	}

	for {
		candidate := filepath.Join(dir, ManifestFile)
		if _, err := os.Stat(candidate); err == nil {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("not in a Frothy project (no %s found)", ManifestFile)
		}
		dir = parent
	}
}

// Load finds froth.toml from startDir (walking up), parses it,
// and returns the manifest plus the project root directory.
func Load(startDir string) (*Manifest, string, error) {
	root, err := FindProjectRoot(startDir)
	if err != nil {
		return nil, "", err
	}

	m, err := LoadManifest(filepath.Join(root, ManifestFile))
	if err != nil {
		return nil, "", err
	}

	if m.Project.Name == "" {
		return nil, "", fmt.Errorf("%s: project.name is required", ManifestFile)
	}

	return m, root, nil
}

// CMakeArgs returns the CMake variable overrides from the [build] section.
// Only non-nil fields are included.
func (b *BuildConfig) CMakeArgs() []string {
	var args []string
	if b.CellSize != nil {
		args = append(args, fmt.Sprintf("-DFROTH_CELL_SIZE_BITS=%d", *b.CellSize))
	}
	if b.HeapSize != nil {
		args = append(args, fmt.Sprintf("-DFROTH_HEAP_SIZE=%d", *b.HeapSize))
	}
	if b.SlotTableSize != nil {
		args = append(args, fmt.Sprintf("-DFROTH_SLOT_TABLE_SIZE=%d", *b.SlotTableSize))
	}
	if b.LineBufferSize != nil {
		args = append(args, fmt.Sprintf("-DFROTH_LINE_BUFFER_SIZE=%d", *b.LineBufferSize))
	}
	if b.TbufSize != nil {
		args = append(args, fmt.Sprintf("-DFROTH_TBUF_SIZE=%d", *b.TbufSize))
	}
	if b.TdescMax != nil {
		args = append(args, fmt.Sprintf("-DFROTH_TDESC_MAX=%d", *b.TdescMax))
	}
	if b.FFIMaxTables != nil {
		args = append(args, fmt.Sprintf("-DFROTH_FFI_MAX_TABLES=%d", *b.FFIMaxTables))
	}
	return args
}
