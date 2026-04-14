package cmd

import (
	_ "embed"
	"strings"
)

//go:embed release-defaults.env
var releaseDefaultsRaw string

var releaseDefaults = parseReleaseDefaults(releaseDefaultsRaw)

func releaseDefault(key string) string {
	return releaseDefaults[key]
}

func parseReleaseDefaults(raw string) map[string]string {
	values := make(map[string]string)
	for _, line := range strings.Split(raw, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		values[strings.TrimSpace(key)] = strings.TrimSpace(value)
	}
	return values
}
