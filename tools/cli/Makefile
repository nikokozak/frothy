REPO_ROOT := ../..
SDK_PAYLOAD_DIR := internal/sdk/generated
OUTPUT ?= froth-cli
export FROTH_LINK_MAX_PAYLOAD ?= 256

.PHONY: sdk-payload build build-test-runner test test-unit test-localruntime test-integration

# The repo root is the source of truth.
sdk-payload:
	@echo "  Generating embedded CLI SDK payload..."
	@go run ./internal/sdk/cmd/generate -repo $(REPO_ROOT) -out $(SDK_PAYLOAD_DIR)

build: sdk-payload
	@echo "  Compiling $(OUTPUT)..."
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go build -o $(OUTPUT) .; \
	else \
		go build -o $(OUTPUT) .; \
	fi

build-test-runner:
	@echo "  Compiling $(OUTPUT)..."
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go build -o $(OUTPUT) ./cmd/test-runner; \
	else \
		go build -o $(OUTPUT) ./cmd/test-runner; \
	fi

test: test-unit test-localruntime test-integration

test-unit: sdk-payload
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test ./...; \
	else \
		go test ./...; \
	fi

test-localruntime: sdk-payload
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test -tags localruntime ./cmd; \
	else \
		go test -tags localruntime ./cmd; \
	fi

test-integration: sdk-payload
	@if [ -n "$(GOCACHE)" ]; then \
		GOCACHE="$(GOCACHE)" go test -tags integration ./cmd; \
	else \
		go test -tags integration ./cmd; \
	fi
