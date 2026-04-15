SHELL := /bin/sh
GO_CACHE_DIR := $(CURDIR)/.cache/go-build
TEST_RUNNER_DIR := $(CURDIR)/build/test/bin
TEST_RUNNER := $(TEST_RUNNER_DIR)/froth-test-runner
TEST_RUNNER_SOURCES := $(shell find tools/cli/cmd/test-runner -type f -name '*.go') tools/cli/go.mod tools/cli/go.sum
.DEFAULT_GOAL := help

.PHONY: help \
	build build-kernel build-cli release run clean clean-kernel clean-cli \
	test test-all test-publishability test-frothy test-cli test-cli-local test-vscode test-vscode-board test-integration test-list test-runner-bin workshop-export-check sdk-payload version version-bump version-check \
	check-cmake check-make check-go

help:
	@awk 'BEGIN {FS = ":.*## "} \
	/^##@/ { printf "\n%s\n", substr($$0, 5); next } \
	/^[A-Za-z0-9_.\/-]+:.*## / { printf "  %-20s %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

##@ Build
build: build-kernel build-cli ## Build everything (kernel + CLI)
	@echo "==> Done. Frothy: build/Frothy, repo-local frothy-cli: tools/cli/frothy-cli"

build-kernel: check-cmake check-make ## Build the Frothy host runtime
	@echo "==> Building Frothy host runtime (POSIX, 32-bit)..."
	@cmake -S . -B build -U 'FROTH_*' -DFROTH_CELL_SIZE_BITS=32 -DFROTHY_BUILD_HOST=ON
	@cmake --build build
	@echo "==> Host runtime ready: build/Frothy"

build-cli: check-go ## Build CLI tool
	@mkdir -p "$(GO_CACHE_DIR)"
	@rm -f tools/cli/froth-cli
	@echo "==> Building repo-local frothy-cli..."
	@$(MAKE) --no-print-directory -C tools/cli build GOCACHE="$(GO_CACHE_DIR)"
	@echo "==> Repo-local frothy-cli ready: tools/cli/frothy-cli"

release: version-check build-cli ## Build release tarball (current platform)
	@tools/package-release.sh

run: build-kernel ## Build host runtimes and launch Frothy
	@exec ./build/Frothy

clean: clean-kernel clean-cli ## Remove all build artifacts
	@rm -rf .cache

clean-kernel: ## Remove kernel build directory
	@rm -rf build

clean-cli: ## Remove repo-local Frothy CLI binaries (not SDK mirror)
	@rm -f tools/cli/frothy-cli tools/cli/froth-cli

##@ Test
test: version-check test-runner-bin ## Run the fast self-contained local test gate
	@$(TEST_RUNNER) fast

test-all: version-check test-runner-bin ## Run the exhaustive local test gate (C, Go, shell only)
	@FROTHY_EDITOR_SMOKE_PORT="$(PORT)" $(TEST_RUNNER) all

test-publishability: version-check test-runner-bin ## Run the full shipped-surface local gate (adds VS Code host smoke)
	@FROTHY_EDITOR_SMOKE_PORT="$(PORT)" $(TEST_RUNNER) publishability

workshop-export-check: ## Verify workshop/pong.frothy matches the canonical v4 base image export
	@sh tools/frothy/export_workshop_repo.sh check

test-frothy: version-check test-runner-bin ## Run Frothy host ctests and proofs
	@$(TEST_RUNNER) frothy

test-cli: version-check test-runner-bin ## Run CLI unit and fake-daemon tests
	@$(TEST_RUNNER) cli

test-cli-local: version-check test-runner-bin ## Run CLI local-runtime tests
	@$(TEST_RUNNER) cli-local

test-vscode: version-check test-runner-bin ## Run VS Code extension tests, package smoke, and host editor smoke
	@$(TEST_RUNNER) vscode

test-vscode-board: version-check test-runner-bin ## Run VS Code board editor smoke on a real device (use PORT=/dev/...)
	@$(TEST_RUNNER) vscode-board --port "$(PORT)"

test-integration: version-check test-runner-bin ## Run CLI project integration tests
	@$(TEST_RUNNER) integration

test-list: version-check test-runner-bin ## List maintained test suites and profiles
	@$(TEST_RUNNER) list

test-runner-bin: $(TEST_RUNNER)

$(TEST_RUNNER): $(TEST_RUNNER_SOURCES)
	@mkdir -p "$(GO_CACHE_DIR)" "$(TEST_RUNNER_DIR)"
	@echo "==> Building test runner..."
	@$(MAKE) --no-print-directory -C tools/cli build-test-runner OUTPUT="$(TEST_RUNNER)" GOCACHE="$(GO_CACHE_DIR)"

bench-frothy: build-kernel ## Run the Frothy runtime benchmark
	@echo "==> Running Frothy runtime benchmark..."
	@./build/frothy_runtime_bench

##@ SDK
sdk-payload: ## Generate the embedded CLI SDK payload from the repo root
	@$(MAKE) --no-print-directory -C tools/cli sdk-payload

##@ Info
version: ## Print current version
	@cat VERSION

version-bump: ## Bump version (PART=major|minor|patch)
	@tools/version-bump.sh $(PART)

version-check:
	@v=$$(cat VERSION); \
	grep -q "^set(FROTH_VERSION \"$$v\" CACHE STRING " CMakeLists.txt || { echo "version mismatch: CMakeLists.txt"; exit 1; }; \
	grep -q "FROTH_VERSION=\"$$v\"" targets/esp-idf/main/CMakeLists.txt || { echo "version mismatch: targets/esp-idf/main/CMakeLists.txt"; exit 1; }

check-cmake:
	@command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is required but was not found on PATH."; exit 1; }

check-make:
	@command -v make >/dev/null 2>&1 || { echo "Error: make is required but was not found on PATH."; exit 1; }

check-go:
	@command -v go >/dev/null 2>&1 || { echo "Error: go is required but was not found on PATH."; exit 1; }
