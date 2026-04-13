#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VSCODE_DIR="$ROOT_DIR/tools/vscode"
CLI_PATH="$ROOT_DIR/tools/cli/froth-cli"
WRAPPER_PATH="$VSCODE_DIR/test/froth-local-runtime-cli.js"
HOST_RUNTIME=${FROTHY_BINARY:-"$ROOT_DIR/build/Frothy"}

usage() {
  cat <<'EOF'
usage:
  proof_editor_smoke.sh --host-only
  proof_editor_smoke.sh <PORT>
EOF
}

ensure_node_deps() {
  command -v node >/dev/null 2>&1 || {
    echo "error: node is required for editor smoke" >&2
    exit 1
  }
  command -v npm >/dev/null 2>&1 || {
    echo "error: npm is required for editor smoke" >&2
    exit 1
  }

  if [ ! -d "$VSCODE_DIR/node_modules" ]; then
    echo "error: missing VS Code dependencies at $VSCODE_DIR/node_modules" >&2
    echo "run: (cd \"$VSCODE_DIR\" && npm ci)" >&2
    exit 1
  fi
}

ensure_cli_binary() {
  if [ -x "$CLI_PATH" ]; then
    return
  fi
  echo "==> Building Frothy CLI for editor smoke"
  (cd "$ROOT_DIR/tools/cli" && make build)
}

if [ "$#" -ne 1 ]; then
  usage >&2
  exit 1
fi

ensure_node_deps
ensure_cli_binary

case "$1" in
  --host-only)
    if [ ! -x "$HOST_RUNTIME" ]; then
      echo "error: missing Frothy runtime: $HOST_RUNTIME" >&2
      exit 1
    fi
    exec node "$VSCODE_DIR/test/run-extension-host-smoke.js" \
      --mode local \
      --cli-path "$WRAPPER_PATH" \
      --real-cli-path "$CLI_PATH" \
      --local-runtime "$HOST_RUNTIME"
    ;;
  *)
    exec node "$VSCODE_DIR/test/run-extension-host-smoke.js" \
      --mode serial \
      --cli-path "$CLI_PATH" \
      --port "$1"
    ;;
esac
