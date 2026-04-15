#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_RUNNER_BIN=${FROTH_TEST_RUNNER_BIN:-}

run_test_runner() {
  if [ -n "${TEST_RUNNER_BIN:-}" ] && [ -x "$TEST_RUNNER_BIN" ]; then
    exec "$TEST_RUNNER_BIN" "$@"
  fi

  if ! command -v go >/dev/null 2>&1; then
    echo "error: go is required for Frothy proof helpers" >&2
    exit 1
  fi

  GOCACHE_DIR=${GOCACHE:-$ROOT_DIR/.cache/go-build}
  mkdir -p "$GOCACHE_DIR"
  cd "$ROOT_DIR/tools/cli"
  exec env GOCACHE="$GOCACHE_DIR" go run ./cmd/test-runner "$@"
}

usage() {
  cat <<'EOF'
usage: proof.sh <proof> [args...]

proofs:
  host
  stack-budget
  control [--host-only|<PORT>]
  editor [--host-only|<PORT>]
  repl
  ctrl-c
  inspect
  boot
  ffi
  safe-boot
  workshop-docs
  workshop-v4 [--skip-live-controls] <PORT>
  m10 [--host-only|--assume-blink-confirmed|--transcript-out PATH|<PORT>]
EOF
}

run_one() {
  proof_name=$1
  shift

  case "$proof_name" in
    repl)
      exec "$SCRIPT_DIR/proof_m8_repl_smoke.sh" "$@"
      ;;
    stack-budget)
      exec sh "$SCRIPT_DIR/proof_eval_stack_budget.sh" "$@"
      ;;
    ctrl-c)
      run_test_runner proof-ctrlc "$@"
      ;;
    control)
      exec sh "$SCRIPT_DIR/proof_f1_control_smoke.sh" "$@"
      ;;
    editor)
      exec sh "$SCRIPT_DIR/proof_editor_smoke.sh" "$@"
      ;;
    inspect)
      exec sh "$SCRIPT_DIR/proof_m8_inspect_smoke.sh" "$@"
      ;;
    boot)
      exec sh "$SCRIPT_DIR/proof_m8_boot_smoke.sh" "$@"
      ;;
    ffi)
      exec sh "$SCRIPT_DIR/proof_m9_ffi_smoke.sh" "$@"
      ;;
    safe-boot)
      run_test_runner proof-safeboot "$@"
      ;;
    workshop-docs)
      exec sh "$SCRIPT_DIR/proof_workshop_ops_docs.sh" "$@"
      ;;
    workshop-v4)
      exec sh "$SCRIPT_DIR/proof_v4_workshop_surface.sh" "$@"
      ;;
    m10)
      exec sh "$SCRIPT_DIR/proof_m10_smoke.sh" "$@"
      ;;
    *)
      usage >&2
      exit 1
      ;;
  esac
}

if [ "$#" -lt 1 ]; then
  usage >&2
  exit 1
fi

case "$1" in
  host)
    shift
    [ "$#" -eq 0 ] || {
      usage >&2
      exit 1
    }
    for proof_name in stack-budget repl ctrl-c control editor inspect boot ffi safe-boot workshop-docs; do
      printf '==> %s\n' "$proof_name"
      if [ "$proof_name" = control ] || [ "$proof_name" = editor ]; then
        sh "$0" "$proof_name" --host-only
      else
        sh "$0" "$proof_name"
      fi
    done
    printf '==> m10 --host-only\n'
    exec sh "$0" m10 --host-only
    ;;
  *)
    proof_name=$1
    shift
    run_one "$proof_name" "$@"
    ;;
esac
