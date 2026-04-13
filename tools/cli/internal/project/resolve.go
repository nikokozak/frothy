package project

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// ResolveResult holds the output of include resolution.
type ResolveResult struct {
	Source   string   // merged source with file boundary markers
	Files    []string // list of files included, in resolution order
	Warnings []string // non-fatal issues (library discipline, duplicates)
}

// Resolve performs include resolution starting from the entry file.
// It reads froth.toml dependencies for named includes and resolves
// relative includes against the containing file's directory.
// ResolveEntry resolves a single entry file path without a manifest.
// Used by `froth send <file>` outside a project.
func ResolveEntry(entryPath string, projectRoot string) (*ResolveResult, error) {
	return doResolve(nil, entryPath, projectRoot)
}

func Resolve(manifest *Manifest, projectRoot string) (*ResolveResult, error) {
	if manifest == nil {
		return nil, fmt.Errorf("manifest is nil (use ResolveEntry for bare mode)")
	}
	entryPath := filepath.Join(projectRoot, manifest.Project.Entry)
	return doResolve(manifest, entryPath, projectRoot)
}

func doResolve(manifest *Manifest, entryPath string, projectRoot string) (*ResolveResult, error) {
	canonRoot, err := canonicalize(projectRoot)
	if err != nil {
		return nil, fmt.Errorf("project root: %w", err)
	}

	r := &resolver{
		manifest:         manifest,
		projectRoot:      projectRoot,
		canonProjectRoot: canonRoot,
		resolved:         make(map[string]bool),
		inProgress:       make(map[string]bool),
		entryPath:        "",
	}

	canon, err := canonicalize(entryPath)
	if err != nil {
		return nil, fmt.Errorf("entry file: %w", err)
	}
	r.entryPath = canon

	source, err := r.resolve(entryPath, true)
	if err != nil {
		return nil, err
	}

	r.warnings = append(r.warnings, checkDuplicateDefinitions(source, r.fileOrder)...)

	return &ResolveResult{
		Source:   source,
		Files:    r.fileOrder,
		Warnings: r.warnings,
	}, nil
}

// StripBoundaryMarkers removes host-only file boundary markers before the
// source is evaluated by a Frothy runtime.
func StripBoundaryMarkers(source string) string {
	lines := strings.Split(source, "\n")
	filtered := lines[:0]
	for _, line := range lines {
		if strings.HasPrefix(line, `\ --- `) {
			continue
		}
		filtered = append(filtered, line)
	}
	return strings.Join(filtered, "\n")
}

type resolver struct {
	manifest         *Manifest
	projectRoot      string
	canonProjectRoot string
	resolved         map[string]bool
	inProgress       map[string]bool
	entryPath        string
	fileOrder        []string
	warnings         []string
}

func (r *resolver) resolve(filePath string, isEntry bool) (string, error) {
	canon, err := canonicalize(filePath)
	if err != nil {
		return "", fmt.Errorf("resolve %s: %w", filePath, err)
	}

	// Root escape check
	canonRoot, err := canonicalize(r.projectRoot)
	if err != nil {
		return "", fmt.Errorf("project root: %w", err)
	}
	if !strings.HasPrefix(canon, canonRoot+string(filepath.Separator)) && canon != canonRoot {
		return "", fmt.Errorf("include escapes project root: %s", filePath)
	}

	// Case sensitivity check (macOS/Windows) — all path components
	if err := checkCase(filePath, r.projectRoot); err != nil {
		return "", err
	}

	if r.resolved[canon] {
		return "", nil
	}

	if r.inProgress[canon] {
		if canon == r.entryPath {
			return "", fmt.Errorf("file includes itself: %s", filePath)
		}
		return "", fmt.Errorf("circular include: %s", filePath)
	}

	r.inProgress[canon] = true
	defer func() {
		delete(r.inProgress, canon)
		r.resolved[canon] = true
	}()

	content, err := os.ReadFile(filePath)
	if err != nil {
		return "", fmt.Errorf("read %s: %w", filePath, err)
	}

	lines := strings.Split(string(content), "\n")
	directives, cleaned := extractDirectives(lines)

	// Fix #1: resolve relative includes from the canonical path's directory,
	// not the original path. This ensures symlinks resolve correctly.
	canonDir := filepath.Dir(canon)

	var result strings.Builder
	for _, d := range directives {
		depPath, err := r.resolveDirective(d, canonDir)
		if err != nil {
			return "", fmt.Errorf("%s:%d: %w", filePath, d.line, err)
		}
		sub, err := r.resolve(depPath, false)
		if err != nil {
			return "", err
		}
		result.WriteString(sub)
	}

	if !isEntry {
		if warns := checkLibraryDiscipline(cleaned, filePath); len(warns) > 0 {
			r.warnings = append(r.warnings, warns...)
		}
	}

	// Use canonical paths for Rel to avoid macOS /var vs /private/var mismatch
	relPath, _ := filepath.Rel(r.canonProjectRoot, canon)
	if relPath == "" {
		relPath = filePath
	}
	result.WriteString(fmt.Sprintf("\\ --- %s ---\n", relPath))
	result.WriteString(cleaned)
	result.WriteString("\n")

	r.fileOrder = append(r.fileOrder, relPath)
	return result.String(), nil
}

type directive struct {
	name string
	line int
}

// extractDirectives scans lines for `\ #use "..."` directives using a
// context-aware scanner. Directives are only recognized when `\` is the
// first non-whitespace character on the line (not mid-line comments).
// Directives inside paren comments and string literals are ignored.
func extractDirectives(lines []string) ([]directive, string) {
	var directives []directive
	var cleaned strings.Builder
	parenDepth := 0
	inString := false

	for lineIdx, line := range lines {
		localParenDepth := parenDepth
		localInString := inString
		isDirective := false

		for i := 0; i < len(line); i++ {
			ch := line[i]

			if localInString {
				if ch == '\\' && i+1 < len(line) {
					i++ // skip escape
				} else if ch == '"' {
					localInString = false
				}
				continue
			}

			if localParenDepth > 0 {
				if ch == '(' {
					localParenDepth++
				} else if ch == ')' {
					localParenDepth--
				}
				continue
			}

			if ch == '"' {
				localInString = true
				continue
			}

			// Fix #3: match reader's rule — `(` is a comment opener when it
			// appears as a standalone token (preceded by whitespace or SOL,
			// followed by whitespace or EOL). The reader tokenizes words by
			// whitespace, so `foo(` is one token, not `foo` + `(`.
			if ch == '(' {
				atStart := i == 0 || line[i-1] == ' ' || line[i-1] == '\t'
				atEnd := i+1 >= len(line) || line[i+1] == ' ' || line[i+1] == '\t'
				if atStart && atEnd {
					localParenDepth++
					continue
				}
			}

			// Fix #2: only recognize `\ #use` when `\` is the first
			// non-whitespace character on the line.
			if ch == '\\' && i+1 < len(line) && line[i+1] == ' ' {
				// Check that everything before `\` is whitespace
				prefix := line[:i]
				if strings.TrimSpace(prefix) == "" {
					rest := strings.TrimSpace(line[i+2:])
					if strings.HasPrefix(rest, "#use ") {
						arg := strings.TrimSpace(rest[5:])
						if len(arg) >= 2 && arg[0] == '"' && arg[len(arg)-1] == '"' {
							directives = append(directives, directive{
								name: arg[1 : len(arg)-1],
								line: lineIdx + 1,
							})
							isDirective = true
						}
					}
				}
				break // rest of line is a comment regardless
			}
		}

		parenDepth = localParenDepth
		// Froth strings don't span lines. Reset inString at line boundaries.
		// Paren comments DO span lines, so parenDepth carries across.
		inString = false

		if isDirective {
			// Replace directive lines with empty lines to preserve line numbering
			// for library discipline warnings.
			if lineIdx < len(lines)-1 {
				cleaned.WriteString("\n")
			}
		} else {
			cleaned.WriteString(line)
			if lineIdx < len(lines)-1 {
				cleaned.WriteString("\n")
			}
		}
	}

	return directives, cleaned.String()
}

func (r *resolver) resolveDirective(d directive, containingDir string) (string, error) {
	name := d.name

	if strings.HasPrefix(name, "./") || strings.HasPrefix(name, "../") {
		path := filepath.Join(containingDir, name)
		if _, err := os.Stat(path); err != nil {
			return "", fmt.Errorf("include not found: %s", name)
		}
		return path, nil
	}

	if r.manifest == nil || r.manifest.Dependencies == nil {
		return "", fmt.Errorf("named include %q requires a froth.toml with [dependencies]", name)
	}

	dep, ok := r.manifest.Dependencies[name]
	if !ok {
		return "", fmt.Errorf("dependency %q not found in [dependencies]", name)
	}

	// Fix #8: reject empty paths
	if dep.Path == "" {
		return "", fmt.Errorf("dependency %q has empty path", name)
	}

	path := filepath.Join(r.projectRoot, dep.Path)

	info, err := os.Stat(path)
	if err != nil {
		return "", fmt.Errorf("dependency %q not found at %s", name, dep.Path)
	}
	if info.IsDir() {
		path = filepath.Join(path, "init.froth")
		if _, err := os.Stat(path); err != nil {
			return "", fmt.Errorf("dependency %q is a directory but has no init.froth", name)
		}
	}

	return path, nil
}

// Fix #5: only swallow os.IsNotExist from EvalSymlinks.
// Permission errors and other failures are real problems.
func canonicalize(path string) (string, error) {
	abs, err := filepath.Abs(filepath.Clean(path))
	if err != nil {
		return "", err
	}
	resolved, err := filepath.EvalSymlinks(abs)
	if err != nil {
		if os.IsNotExist(err) {
			return abs, nil
		}
		return "", fmt.Errorf("canonicalize %s: %w", path, err)
	}
	return resolved, nil
}

// Fix #4: checkCase validates ALL path components, not just the basename.
// Walks from projectRoot down to the file, checking each component.
func checkCase(path string, projectRoot string) error {
	if runtime.GOOS != "darwin" && runtime.GOOS != "windows" {
		return nil
	}

	abs, err := filepath.Abs(path)
	if err != nil {
		return nil
	}
	absRoot, err := filepath.Abs(projectRoot)
	if err != nil {
		return nil
	}

	// Get the relative portion within the project
	rel, err := filepath.Rel(absRoot, abs)
	if err != nil {
		return nil
	}

	parts := strings.Split(rel, string(filepath.Separator))
	current := absRoot

	for _, part := range parts {
		if part == "." || part == "" {
			continue
		}

		entries, err := os.ReadDir(current)
		if err != nil {
			return nil // directory doesn't exist, will be caught later
		}

		found := false
		for _, e := range entries {
			if strings.EqualFold(e.Name(), part) {
				if e.Name() != part {
					return fmt.Errorf("case mismatch: path says %q but on disk is %q in %s",
						part, e.Name(), current)
				}
				found = true
				break
			}
		}
		if !found {
			return nil // not found, will be caught later
		}
		current = filepath.Join(current, part)
	}

	return nil
}

// checkLibraryDiscipline warns if a non-entry file has top-level executable forms.
func checkLibraryDiscipline(source string, filePath string) []string {
	// Opt-out: \ #allow-toplevel must be the first non-empty line.
	// Strip UTF-8 BOM if present (Windows editors sometimes write it).
	clean := strings.TrimPrefix(source, "\xEF\xBB\xBF")
	for _, line := range strings.Split(clean, "\n") {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}
		if trimmed == "\\ #allow-toplevel" {
			return nil
		}
		break // first non-empty line is not the pragma
	}

	var warnings []string
	lines := strings.Split(source, "\n")
	inDefinition := false
	parenDepth := 0

	for lineIdx, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			continue
		}

		// Track multi-line paren comments character by character.
		// If the comment closes mid-line, process the remainder.
		if parenDepth > 0 {
			remainder := ""
			for i := 0; i < len(trimmed); i++ {
				if trimmed[i] == '(' {
					parenDepth++
				} else if trimmed[i] == ')' {
					parenDepth--
					if parenDepth == 0 {
						remainder = strings.TrimSpace(trimmed[i+1:])
						break
					}
				}
			}
			if parenDepth > 0 || remainder == "" {
				continue
			}
			trimmed = remainder
			// Fall through to check the remainder
		}

		// Skip line comments
		if strings.HasPrefix(trimmed, "\\") {
			continue
		}

		// Check if line opens a paren comment
		if strings.HasPrefix(trimmed, "( ") || trimmed == "(" {
			parenDepth = 1
			remainder := ""
			for i := 1; i < len(trimmed); i++ {
				if trimmed[i] == '(' {
					parenDepth++
				} else if trimmed[i] == ')' {
					parenDepth--
					if parenDepth == 0 {
						remainder = strings.TrimSpace(trimmed[i+1:])
						break
					}
				}
			}
			if parenDepth > 0 || remainder == "" {
				continue
			}
			trimmed = remainder
			// Fall through to check the remainder
		}

		// Track : ... ; blocks
		if strings.HasPrefix(trimmed, ": ") || trimmed == ":" {
			if containsTopLevelSemicolon(trimmed) {
				// Single-line definition — but warn if there's content after `;`
				afterSemi := contentAfterSemicolon(trimmed)
				if afterSemi != "" {
					warnings = append(warnings, fmt.Sprintf(
						"%s:%d: top-level form after definition: %s",
						filePath, lineIdx+1, truncate(afterSemi, 60),
					))
				}
				continue
			}
			inDefinition = true
			continue
		}
		if containsTopLevelSemicolon(trimmed) && inDefinition {
			afterSemi := contentAfterSemicolon(trimmed)
			inDefinition = false
			if afterSemi != "" {
				warnings = append(warnings, fmt.Sprintf(
					"%s:%d: top-level form after definition: %s",
					filePath, lineIdx+1, truncate(afterSemi, 60),
				))
			}
			continue
		}

		if inDefinition {
			continue
		}

		// Only skip tick-def patterns that end with `def`.
		if strings.HasPrefix(trimmed, "'") && strings.HasSuffix(trimmed, " def") {
			continue
		}

		warnings = append(warnings, fmt.Sprintf(
			"%s:%d: top-level form in library: %s",
			filePath, lineIdx+1, truncate(trimmed, 60),
		))
	}

	return warnings
}

// containsTopLevelSemicolon checks if a line contains `;` outside of
// string literals and paren comments.
func containsTopLevelSemicolon(line string) bool {
	inStr := false
	parenDepth := 0
	for i := 0; i < len(line); i++ {
		ch := line[i]
		if inStr {
			if ch == '\\' && i+1 < len(line) {
				i++
			} else if ch == '"' {
				inStr = false
			}
			continue
		}
		if parenDepth > 0 {
			if ch == '(' {
				parenDepth++
			} else if ch == ')' {
				parenDepth--
			}
			continue
		}
		if ch == '"' {
			inStr = true
			continue
		}
		if ch == '(' {
			atStart := i == 0 || line[i-1] == ' ' || line[i-1] == '\t'
			atEnd := i+1 >= len(line) || line[i+1] == ' ' || line[i+1] == '\t'
			if atStart && atEnd {
				parenDepth++
				continue
			}
		}
		if ch == ';' {
			return true
		}
	}
	return false
}

// checkDuplicateDefinitions scans merged source for `: name` definitions
// that appear in multiple library files. Duplicates within the same file
// or between a library and the entry file (last in fileOrder) are allowed.
// Known limitation: this is line-level heuristic parsing. A `: name` that
// appears inside a quotation body (e.g., `: outer [ : inner 1 ; ] ;`) will
// be incorrectly counted as a top-level definition. This is acceptable for
// the warning it produces; a proper fix requires a real parser.
func checkDuplicateDefinitions(source string, fileOrder []string) []string {
	if len(fileOrder) < 2 {
		return nil
	}
	entryFile := fileOrder[len(fileOrder)-1]

	type defLocation struct {
		name string
		file string
	}

	// Map: definition name → first file it was seen in
	seen := make(map[string]string)
	var warnings []string

	currentFile := ""
	for _, line := range strings.Split(source, "\n") {
		trimmed := strings.TrimSpace(line)

		// Track file boundaries
		if strings.HasPrefix(trimmed, "\\ --- ") && strings.HasSuffix(trimmed, " ---") {
			currentFile = strings.TrimSpace(trimmed[5 : len(trimmed)-4])
			continue
		}

		// Skip entry file definitions (allowed to override)
		if currentFile == entryFile {
			continue
		}

		// Look for `: name` definitions
		if strings.HasPrefix(trimmed, ": ") && len(trimmed) > 2 {
			// Extract the name (first word after `:`)
			rest := trimmed[2:]
			spaceIdx := strings.IndexAny(rest, " \t")
			name := rest
			if spaceIdx > 0 {
				name = rest[:spaceIdx]
			}

			if prev, ok := seen[name]; ok && prev != currentFile {
				warnings = append(warnings, fmt.Sprintf(
					"duplicate definition %q in %s and %s",
					name, prev, currentFile,
				))
			} else if !ok {
				seen[name] = currentFile
			}
		}
	}

	return warnings
}

// contentAfterSemicolon returns any non-whitespace content after the
// first top-level `;` on a line, ignoring comments. Returns "" if the
// `;` is the last meaningful content.
func contentAfterSemicolon(line string) string {
	inStr := false
	parenDepth := 0
	for i := 0; i < len(line); i++ {
		ch := line[i]
		if inStr {
			if ch == '\\' && i+1 < len(line) {
				i++
			} else if ch == '"' {
				inStr = false
			}
			continue
		}
		if parenDepth > 0 {
			if ch == '(' {
				parenDepth++
			} else if ch == ')' {
				parenDepth--
			}
			continue
		}
		if ch == '"' {
			inStr = true
			continue
		}
		if ch == '(' {
			atStart := i == 0 || line[i-1] == ' ' || line[i-1] == '\t'
			atEnd := i+1 >= len(line) || line[i+1] == ' ' || line[i+1] == '\t'
			if atStart && atEnd {
				parenDepth++
				continue
			}
		}
		if ch == ';' {
			rest := strings.TrimSpace(line[i+1:])
			// Ignore trailing line comments
			if rest == "" || strings.HasPrefix(rest, "\\") {
				return ""
			}
			return rest
		}
	}
	return ""
}

func truncate(s string, max int) string {
	if len(s) <= max {
		return s
	}
	return s[:max-3] + "..."
}
