package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/nikokozak/froth/tools/cli/internal/frothycontrol"
)

func main() {
	var cfg frothycontrol.SmokeConfig
	flag.StringVar(&cfg.Port, "port", "", "serial port for the device control session")
	flag.StringVar(&cfg.LocalRuntime, "local-runtime", "", "path to a local Frothy runtime binary")
	flag.Parse()

	if cfg.Port == "" && cfg.LocalRuntime == "" {
		fmt.Fprintln(os.Stderr, "error: control smoke requires --port or --local-runtime")
		os.Exit(1)
	}
	if cfg.Port != "" && cfg.LocalRuntime != "" {
		fmt.Fprintln(os.Stderr, "error: control smoke accepts only one of --port or --local-runtime")
		os.Exit(1)
	}
	if err := frothycontrol.RunSmoke(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}
