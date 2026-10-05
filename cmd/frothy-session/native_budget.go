package main

import (
	"errors"
	"fmt"
	"path/filepath"
	"strconv"
	"strings"
)

// The firmware installs the base natives first and the library natives after
// them. When the profile's native table is full, fr_base_image_install fails
// and the board stops at boot, so frothy build refuses that set up front.
type nativeBudget struct {
	baseRows     int
	tableSize    int
	libraryNames int // name records for library natives (FR_LIB_NATIVE_RECORD_MAX)
}

func (b nativeBudget) free() int { return b.tableSize - b.baseRows }

// Tests replace this to avoid compiling the host probe.
var readNativeBudgetFn = readNativeBudget

func readNativeBudget(sourceRoot, board, compositionH string) (nativeBudget, error) {
	facts, err := readMakeFacts(sourceRoot, "print-native-budget",
		"the native-row check builds a host program and needs a host C compiler",
		board, compositionH)
	if err != nil {
		return nativeBudget{}, err
	}
	rows, rowsErr := strconv.Atoi(facts["NATIVE_BASE_ROWS"])
	size, sizeErr := strconv.Atoi(facts["NATIVE_TABLE_SIZE"])
	names, namesErr := strconv.Atoi(facts["NATIVE_LIBRARY_NAMES"])
	if rowsErr != nil || sizeErr != nil || namesErr != nil || size <= 0 {
		return nativeBudget{}, fmt.Errorf("native-row check: unexpected output %v", facts)
	}
	return nativeBudget{baseRows: rows, tableSize: size, libraryNames: names}, nil
}

func libraryNativeCount(libs []resolvedLibrary) int {
	count := 0
	for _, lib := range libs {
		count += len(lib.natives)
	}
	return count
}

// checkNativeBudget reports the limit that the boot install meets first: each
// library native takes a native row and then a name record.
func checkNativeBudget(board string, budget nativeBudget, libs []resolvedLibrary) error {
	need := libraryNativeCount(libs)
	if need <= budget.free() && need <= budget.libraryNames {
		return nil
	}
	var parts []string
	for _, lib := range libs {
		if len(lib.natives) > 0 {
			parts = append(parts, fmt.Sprintf("%s %d", lib.name, len(lib.natives)))
		}
	}
	if budget.libraryNames < budget.free() {
		return fmt.Errorf(
			"board %s holds names for at most %d library natives, but the libraries need %d (%s); remove a library",
			board, budget.libraryNames, need, strings.Join(parts, ", "))
	}
	message := fmt.Sprintf(
		"board %s has %d free native rows (the base uses %d of %d) but the libraries need %d (%s); remove a library",
		board, budget.free(), budget.baseRows, budget.tableSize, need,
		strings.Join(parts, ", "))
	// A larger table helps only when the name records also hold the natives.
	if need <= budget.libraryNames {
		message += "\nnote: a larger FR_PROFILE_NATIVE_TABLE_SIZE also fits them, but it changes the profile hash, so saved programs must be sent again"
	}
	return errors.New(message)
}

// verifyNativeBudget runs before the firmware build, and only when a library
// adds natives, so a project without natives never pays for the host probe.
func verifyNativeBudget(projectDir, board string, libs []resolvedLibrary) error {
	if libraryNativeCount(libs) == 0 {
		return nil
	}
	sourceRoot, err := resolveFrothySourceRoot(projectDir)
	if err != nil {
		return err
	}
	compositionH := filepath.Join(buildOutputDir(projectDir, board), "composition.h")
	if !fileExists(compositionH) {
		compositionH = ""
	}
	budget, err := readNativeBudgetFn(sourceRoot, board, compositionH)
	if err != nil {
		return err
	}
	return checkNativeBudget(board, budget, libs)
}
