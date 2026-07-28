package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestResolveTargetContracts(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}

	tests := []struct {
		board     string
		buildKind string
		available []string
		missing   map[string]capabilityReason
	}{
		{
			board:     "host",
			buildKind: "host",
			available: []string{"adc", "gpio", "pwm"},
			missing: map[string]capabilityReason{
				"ble":   capabilityProfileDisabled,
				"cells": capabilityProfileDisabled,
				"i2s":   capabilityHardwareAbsent,
				"net":   capabilityProfileDisabled,
				"uart":  capabilityProfileDisabled,
			},
		},
		{
			board:     "esp32_devkit_v1",
			buildKind: "esp-idf",
			available: []string{"ble", "cells", "dual_core", "i2s", "net", "pwm", "uart"},
		},
		{
			board:     "seeed_xiao_esp32s3",
			buildKind: "esp-idf",
			available: []string{"ble", "cells", "dual_core", "i2s", "net", "pwm", "uart"},
		},
		{
			board:     "arduino_nano_rp2040_connect",
			buildKind: "arduino-pico",
			available: []string{"ble", "cells", "dual_core", "i2s", "net", "pwm"},
			missing:   map[string]capabilityReason{"uart": capabilityTargetUnimplemented},
		},
		{
			board:     "seeed_xiao_rp2040",
			buildKind: "arduino-pico",
			available: []string{"cells", "dual_core", "i2s", "pwm"},
			missing: map[string]capabilityReason{
				"ble":  capabilityHardwareAbsent,
				"net":  capabilityHardwareAbsent,
				"uart": capabilityTargetUnimplemented,
			},
		},
		{
			board:     "seeed_xiao_esp32c6",
			buildKind: "esp-idf",
			available: []string{"ble", "cells", "i2s", "net", "pwm", "uart"},
			missing:   map[string]capabilityReason{"dual_core": capabilityHardwareAbsent},
		},
	}
	for _, test := range tests {
		t.Run(test.board, func(t *testing.T) {
			contract, err := resolveTargetContract(root, test.board, nil, "")
			if err != nil {
				t.Fatal(err)
			}
			if contract.buildKind != test.buildKind || contract.wordBits != 32 {
				t.Fatalf("build kind/ABI = %q/%d", contract.buildKind, contract.wordBits)
			}
			if len(contract.capabilities) != len(capabilities) {
				t.Fatalf("resolved %d capabilities, want %d",
					len(contract.capabilities), len(capabilities))
			}
			for _, name := range test.available {
				if status := contract.capabilities[name]; !status.available {
					t.Errorf("%s unavailable: %s", name, status.reason)
				}
			}
			for name, reason := range test.missing {
				if status := contract.capabilities[name]; status.available ||
					status.reason != reason {
					t.Errorf("%s = %+v, want unavailable/%s", name, status, reason)
				}
			}
		})
	}
}

func TestTargetContractCompositionAndAdmission(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	compositionH := filepath.Join(t.TempDir(), "composition.h")
	if err := os.WriteFile(compositionH, []byte(
		"#define FR_FEATURE_BLE 0\n#define FR_FEATURE_NET 0\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	selected := map[string]bool{"ble": false, "net": false}
	baseFacts, err := readTargetFacts(root, "arduino_nano_rp2040_connect", "")
	if err != nil {
		t.Fatal(err)
	}
	resolvedFacts, err := readTargetFacts(
		root, "arduino_nano_rp2040_connect", compositionH)
	if err != nil {
		t.Fatal(err)
	}
	if baseFacts["FR_FEATURE_BLE"] != "1" ||
		resolvedFacts["FR_FEATURE_BLE"] != "0" {
		t.Fatalf("BLE facts before/after composition = %q/%q",
			baseFacts["FR_FEATURE_BLE"], resolvedFacts["FR_FEATURE_BLE"])
	}
	contract, err := resolveTargetContract(
		root, "arduino_nano_rp2040_connect", selected, compositionH)
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"ble", "net"} {
		status := contract.capabilities[name]
		if status.available || status.reason != capabilityCompositionDisabled {
			t.Errorf("%s = %+v, want composition_disabled", name, status)
		}
	}

	xiao, err := resolveTargetContract(root, "seeed_xiao_rp2040",
		map[string]bool{"ble": true}, "")
	if err != nil {
		t.Fatal(err)
	}
	err = validateEnabledCapabilities(xiao, map[string]bool{"ble": true})
	if err == nil || !strings.Contains(err.Error(), "hardware_absent") {
		t.Fatalf("want active XIAO BLE rejection, got %v", err)
	}
}

func TestCapabilityReasonPrecedence(t *testing.T) {
	statuses := resolveCapabilityStatuses(
		map[string]bool{"ble": true, "cells": true, "i2s": true, "net": true},
		map[string]bool{"ble": true, "net": true},
		map[string]bool{
			"FR_FEATURE_BLE":   false,
			"FR_FEATURE_CELLS": false,
			"FR_FEATURE_NET":   true,
		},
		map[string]bool{"ble": false, "net": false},
	)
	if got := statuses["i2s"].reason; got != capabilityTargetUnimplemented {
		t.Fatalf("i2s reason = %s", got)
	}
	if got := statuses["cells"].reason; got != capabilityProfileDisabled {
		t.Fatalf("cells reason = %s", got)
	}
	if got := statuses["ble"].reason; got != capabilityProfileDisabled {
		t.Fatalf("ble reason = %s", got)
	}
	if got := statuses["net"].reason; got != capabilityCompositionDisabled {
		t.Fatalf("net reason = %s", got)
	}
}

func TestTargetFactCompilerFailureIsReported(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv("PROFILE_REPORT_CC", "false")
	if _, err := readTargetFacts(root, "host", ""); err == nil ||
		!strings.Contains(err.Error(), "resolve target facts") {
		t.Fatalf("want preprocessor failure, got %v", err)
	}
}

func TestTargetFactsIgnoreInheritedMakeDryRun(t *testing.T) {
	root, err := resolveFrothySourceRoot(".")
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv("MAKEFLAGS", "-n")
	facts, err := readTargetFacts(root, "host", "")
	if err != nil {
		t.Fatal(err)
	}
	if facts["TARGET"] != "host" || facts["FR_WORD_SIZE"] != "32" {
		t.Fatalf("facts under MAKEFLAGS=-n: %+v", facts)
	}
}

func TestSafeBoardID(t *testing.T) {
	for _, id := range []string{"host", "seeed_xiao_rp2040", "board-2"} {
		if !safeBoardID(id) {
			t.Errorf("safeBoardID(%q) = false", id)
		}
	}
	for _, id := range []string{"", "_host", "../host", "Host", "x y", "$(shell false)"} {
		if safeBoardID(id) {
			t.Errorf("safeBoardID(%q) = true", id)
		}
	}
}
