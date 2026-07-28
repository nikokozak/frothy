package main

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type capabilityReason string

// These tokens are part of target-contract schema 1. New reasons may be added
// without changing the schema; renaming, removing, or changing the meaning of
// an existing reason requires a new schema.
const (
	capabilityHardwareAbsent      capabilityReason = "hardware_absent"
	capabilityTargetUnimplemented capabilityReason = "target_unimplemented"
	capabilityProfileDisabled     capabilityReason = "profile_disabled"
	capabilityCompositionDisabled capabilityReason = "composition_disabled"
)

type capabilityStatus struct {
	available bool
	reason    capabilityReason
}

type targetContract struct {
	board        string
	target       string
	buildKind    string
	profile      string
	wordBits     int
	capabilities map[string]capabilityStatus
}

const targetContractSchema = 1

type publishedCapabilityStatus struct {
	Available bool             `json:"available"`
	Reason    capabilityReason `json:"reason,omitempty"`
}

type publishedTargetContract struct {
	Schema       int                                  `json:"schema"`
	Board        string                               `json:"board"`
	Target       string                               `json:"target"`
	BuildKind    string                               `json:"build_kind"`
	Profile      string                               `json:"profile"`
	Capabilities map[string]publishedCapabilityStatus `json:"capabilities"`
}

func publishTargetContract(contract targetContract) publishedTargetContract {
	statuses := make(map[string]publishedCapabilityStatus, len(contract.capabilities))
	for name, status := range contract.capabilities {
		statuses[name] = publishedCapabilityStatus{
			Available: status.available,
			Reason:    status.reason,
		}
	}
	return publishedTargetContract{
		Schema:       targetContractSchema,
		Board:        contract.board,
		Target:       contract.target,
		BuildKind:    contract.buildKind,
		Profile:      contract.profile,
		Capabilities: statuses,
	}
}

func encodeTargetContract(contract targetContract) ([]byte, error) {
	encoded, err := json.MarshalIndent(publishTargetContract(contract), "", "  ")
	if err != nil {
		return nil, err
	}
	return append(encoded, '\n'), nil
}

func emitTargetContract(path string, contract targetContract) error {
	encoded, err := encodeTargetContract(contract)
	if err != nil {
		return fmt.Errorf("encode target contract: %w", err)
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), "."+filepath.Base(path)+".tmp-*")
	if err != nil {
		return fmt.Errorf("create target contract: %w", err)
	}
	tmpPath := tmp.Name()
	defer os.Remove(tmpPath)
	if err = tmp.Chmod(0o644); err == nil {
		_, err = tmp.Write(encoded)
	}
	if closeErr := tmp.Close(); err == nil {
		err = closeErr
	}
	if err == nil {
		err = os.Rename(tmpPath, path)
	}
	if err != nil {
		return fmt.Errorf("write target contract: %w", err)
	}
	return nil
}

func resolveTargetContract(sourceRoot, board string, selected map[string]bool,
	compositionH string) (targetContract, error) {
	if compositionH != "" {
		var err error
		compositionH, err = filepath.Abs(compositionH)
		if err != nil {
			return targetContract{}, fmt.Errorf("resolve composition header: %w", err)
		}
		if !fileExists(compositionH) {
			return targetContract{}, fmt.Errorf(
				"composition header does not exist: %s", compositionH)
		}
	}
	manifest, err := readBoardManifest(filepath.Join(sourceRoot, "boards"), board)
	if err != nil {
		return targetContract{}, err
	}
	if manifest.Target == "" || manifest.Profile == "" ||
		manifest.Cores < 1 || manifest.Peripherals == nil {
		return targetContract{}, fmt.Errorf("board %q is incomplete", board)
	}

	profileFacts, err := readTargetFacts(sourceRoot, board, "")
	if err != nil {
		return targetContract{}, err
	}
	resolvedFacts := profileFacts
	if compositionH != "" {
		resolvedFacts, err = readTargetFacts(sourceRoot, board, compositionH)
		if err != nil {
			return targetContract{}, err
		}
	}
	if profileFacts["TARGET"] != manifest.Target {
		return targetContract{}, fmt.Errorf(
			"board %q target mismatch: board.json has %q, Make selected %q",
			board, manifest.Target, profileFacts["TARGET"])
	}
	if profileFacts["PROFILE"] != manifest.Profile {
		return targetContract{}, fmt.Errorf(
			"board %q profile mismatch: board.json has %q, Make selected %q",
			board, manifest.Profile, profileFacts["PROFILE"])
	}
	if profileFacts["TARGET_BUILD_KIND"] == "" {
		return targetContract{}, fmt.Errorf("target %q has no build kind", manifest.Target)
	}

	targetCapabilities := make(map[string]bool)
	for _, name := range strings.Fields(profileFacts["TARGET_CAPABILITIES"]) {
		c, ok := capabilityByName(name)
		if !ok || !c.needsTarget {
			return targetContract{}, fmt.Errorf(
				"target %q declares invalid capability %q", manifest.Target, name)
		}
		if targetCapabilities[name] {
			return targetContract{}, fmt.Errorf(
				"target %q declares capability %q more than once",
				manifest.Target, name)
		}
		targetCapabilities[name] = true
	}
	boardCapabilities := make(map[string]bool)
	for _, name := range manifest.Peripherals {
		c, ok := capabilityByName(name)
		if !ok || !c.boardPeripheral {
			return targetContract{}, fmt.Errorf(
				"board %q declares invalid peripheral %q", board, name)
		}
		if boardCapabilities[name] {
			return targetContract{}, fmt.Errorf(
				"board %q declares peripheral %q more than once", board, name)
		}
		boardCapabilities[name] = true
	}
	if manifest.Cores > 1 {
		boardCapabilities["dual_core"] = true
	}

	profileCapabilities := make(map[string]bool)
	for _, c := range capabilities {
		if c.profileMacro == "" {
			continue
		}
		profileValue, err := parseBinaryFact(profileFacts, c.profileMacro)
		if err != nil {
			return targetContract{}, fmt.Errorf("profile %q: %w", manifest.Profile, err)
		}
		resolvedValue, err := parseBinaryFact(resolvedFacts, c.profileMacro)
		if err != nil {
			return targetContract{}, fmt.Errorf("resolved profile %q: %w", manifest.Profile, err)
		}
		expected := profileValue
		if c.toggleable && explicitlyDisabled(selected, c.name) {
			expected = false
		}
		if resolvedValue != expected {
			return targetContract{}, fmt.Errorf(
				"composition resolved %s=%d, want %d",
				c.profileMacro, binaryInt(resolvedValue), binaryInt(expected))
		}
		profileCapabilities[c.profileMacro] = profileValue
	}
	wordBits, err := strconv.Atoi(profileFacts["FR_WORD_SIZE"])
	if err != nil || wordBits <= 0 {
		return targetContract{}, fmt.Errorf(
			"profile %q has invalid FR_WORD_SIZE %q",
			manifest.Profile, profileFacts["FR_WORD_SIZE"])
	}
	if resolvedFacts["FR_WORD_SIZE"] != profileFacts["FR_WORD_SIZE"] {
		return targetContract{}, fmt.Errorf(
			"composition changed FR_WORD_SIZE from %s to %s",
			profileFacts["FR_WORD_SIZE"], resolvedFacts["FR_WORD_SIZE"])
	}

	return targetContract{
		board:     board,
		target:    manifest.Target,
		buildKind: profileFacts["TARGET_BUILD_KIND"],
		profile:   manifest.Profile,
		wordBits:  wordBits,
		capabilities: resolveCapabilityStatuses(
			boardCapabilities, targetCapabilities, profileCapabilities, selected),
	}, nil
}

func readTargetFacts(sourceRoot, board, compositionH string) (map[string]string, error) {
	args := []string{
		"-s", "--no-print-directory", "-C", sourceRoot,
		"print-target-facts", "BOARD=" + board,
		"FROTHY_COMPOSITION_H=" + compositionH,
	}
	cmd := exec.Command("make", args...)
	cmd.Env = makeFactEnvironment()
	var stderr strings.Builder
	cmd.Stderr = &stderr
	output, err := cmd.Output()
	if err != nil {
		message := strings.TrimSpace(stderr.String())
		if message == "" {
			return nil, fmt.Errorf("resolve target facts: %w", err)
		}
		return nil, fmt.Errorf("resolve target facts: %w: %s", err, message)
	}
	facts := make(map[string]string)
	for _, line := range strings.Split(string(output), "\n") {
		key, value, ok := strings.Cut(line, "=")
		if ok {
			if _, exists := facts[key]; exists {
				return nil, fmt.Errorf("resolve target facts: duplicate %s", key)
			}
			facts[key] = value
		}
	}
	return facts, nil
}

func makeFactEnvironment() []string {
	env := make([]string, 0, len(os.Environ())+2)
	for _, value := range os.Environ() {
		if strings.HasPrefix(value, "MAKEFLAGS=") ||
			strings.HasPrefix(value, "MFLAGS=") {
			continue
		}
		env = append(env, value)
	}
	return append(env, "MAKEFLAGS=", "MFLAGS=")
}

func parseBinaryFact(facts map[string]string, name string) (bool, error) {
	switch facts[name] {
	case "0":
		return false, nil
	case "1":
		return true, nil
	default:
		return false, fmt.Errorf("has invalid %s %q", name, facts[name])
	}
}

func binaryInt(value bool) int {
	if value {
		return 1
	}
	return 0
}

func resolveCapabilityStatuses(board, target, profile map[string]bool,
	selected map[string]bool) map[string]capabilityStatus {
	resolved := make(map[string]capabilityStatus, len(capabilities))
	for _, c := range capabilities {
		status := capabilityStatus{available: true}
		switch {
		case c.needsBoard && !board[c.name]:
			status = capabilityStatus{reason: capabilityHardwareAbsent}
		case c.needsTarget && !target[c.name]:
			status = capabilityStatus{reason: capabilityTargetUnimplemented}
		case c.profileMacro != "" && !profile[c.profileMacro]:
			status = capabilityStatus{reason: capabilityProfileDisabled}
		case c.toggleable && explicitlyDisabled(selected, c.name):
			status = capabilityStatus{reason: capabilityCompositionDisabled}
		}
		resolved[c.name] = status
	}
	return resolved
}

func explicitlyDisabled(selected map[string]bool, name string) bool {
	enabled, present := selected[name]
	return present && !enabled
}

func needsTargetContract(selected map[string]bool, libs []resolvedLibrary) bool {
	if len(selected) > 0 {
		return true
	}
	for _, lib := range libs {
		if len(lib.requires) > 0 {
			return true
		}
	}
	return false
}

func validateEnabledCapabilities(contract targetContract,
	selected map[string]bool) error {
	var names []string
	for name, enabled := range selected {
		if enabled {
			names = append(names, name)
		}
	}
	sort.Strings(names)
	for _, name := range names {
		status, ok := contract.capabilities[name]
		if !ok {
			return fmt.Errorf("capability %q was not resolved", name)
		}
		if !status.available {
			return fmt.Errorf(
				"capability %q is unavailable on board %s: %s",
				name, contract.board, status.reason)
		}
	}
	return nil
}
