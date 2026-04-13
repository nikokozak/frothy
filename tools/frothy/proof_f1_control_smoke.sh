#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BINARY="${FROTHY_BINARY:-$ROOT_DIR/build/Frothy}"
HOST_ONLY=0
PORT=

usage() {
  cat >&2 <<'EOF'
usage:
  proof_f1_control_smoke.sh --host-only
  proof_f1_control_smoke.sh <PORT>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --host-only)
      HOST_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      usage
      exit 1
      ;;
    *)
      PORT=$1
      shift
      ;;
  esac
done

if [ "$HOST_ONLY" -eq 1 ] && [ -n "$PORT" ]; then
  usage
  exit 1
fi

if [ "$HOST_ONLY" -ne 1 ] && [ -z "$PORT" ]; then
  usage
  exit 1
fi

if [ ! -x "$BINARY" ]; then
  echo "error: $BINARY is missing; run cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

if ! command -v go >/dev/null 2>&1; then
  echo "error: go is required for the control smoke tool" >&2
  exit 1
fi

run_local_smoke() {
  (
    cd "$ROOT_DIR/tools/cli"
    go run ./cmd/frothycontrol-smoke --local-runtime "$BINARY"
  )
}

run_device_smoke() {
  (
    cd "$ROOT_DIR/tools/cli"
    go run ./cmd/frothycontrol-smoke --port "$PORT"
  )
}

build_cli_binary() {
  cli_bin=$1
  gocache_dir=${GOCACHE:-$ROOT_DIR/.cache/go-build}
  mkdir -p "$gocache_dir"
  if ! command -v make >/dev/null 2>&1; then
    echo "error: make is required for the flash/apply proof path" >&2
    exit 1
  fi
  (
    cd "$ROOT_DIR/tools/cli"
    make --no-print-directory build OUTPUT="$cli_bin" GOCACHE="$gocache_dir"
  )
}

run_flash_apply_smoke() {
  work_dir="$(mktemp -d "${TMPDIR:-/tmp}/frothy-f1-control.XXXXXX")"
  cli_bin="$work_dir/froth"
  project_dir="$work_dir/flash-proof"

  cleanup() {
    rm -rf "$work_dir"
  }
  trap cleanup EXIT HUP INT TERM

  build_cli_binary "$cli_bin"
  "$cli_bin" new --target esp32-devkit-v1 "$project_dir"
  cat >"$project_dir/src/main.froth" <<'EOF'
note = "flash-proof"
EOF

  (
    cd "$project_dir"
    "$cli_bin" build
    "$cli_bin" --port "$PORT" flash
  )

  verify_one="$("$cli_bin" --port "$PORT" send note)"
  printf '%s\n' "$verify_one"
  require_contains "$verify_one" '"flash-proof"'

  verify_two="$("$cli_bin" --port "$PORT" send note)"
  printf '%s\n' "$verify_two"
  require_contains "$verify_two" '"flash-proof"'
}

require_contains() {
  transcript=$1
  needle=$2
  if ! printf '%s\n' "$transcript" | grep -F "$needle" >/dev/null; then
    echo "error: expected output to contain: $needle" >&2
    exit 1
  fi
}

run_local_smoke

if [ "$HOST_ONLY" -eq 1 ]; then
  exit 0
fi

if [ ! -e "$PORT" ]; then
  echo "error: serial port is missing: $PORT" >&2
  exit 1
fi

run_device_smoke
run_flash_apply_smoke
