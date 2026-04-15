package project

import "path/filepath"

// StarterSourceFiles returns the source files scaffolded by `froth new`
// for the requested board target.
func StarterSourceFiles(board string) map[string]string {
	return defaultStarterSourceFiles()
}

func defaultStarterSourceFiles() map[string]string {
	return map[string]string{
		filepath.Join("src", "main.froth"): `note = nil

boot {
  set note = "Hello from Frothy!"
}
`,
	}
}
