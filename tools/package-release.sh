#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT_DIR/tools/release-common.sh"

if [ "$#" -eq 0 ]; then
  VERSION=$(normalize_version "$(cat "$ROOT_DIR/VERSION")")
  # Default to the repo-local `froth-cli` checkout build when packaging locally.
  BINARY="$ROOT_DIR/tools/cli/froth-cli"
  DIST_DIR="$ROOT_DIR/dist"
  GOOS=$(detect_goos)
  GOARCH=$(detect_goarch)
elif [ "$#" -eq 4 ]; then
  BINARY=$1
  VERSION=$(normalize_version "$2")
  GOOS=$3
  GOARCH=$4
  DIST_DIR="$ROOT_DIR/dist"
elif [ "$#" -eq 5 ]; then
  BINARY=$1
  VERSION=$(normalize_version "$2")
  GOOS=$3
  GOARCH=$4
  DIST_DIR=$5
else
  printf 'usage: %s [binary version os arch [dist-dir]]\n' "${0##*/}" >&2
  exit 1
fi

if [ ! -f "$BINARY" ]; then
  printf 'missing CLI binary: %s\n' "$BINARY" >&2
  exit 1
fi

mkdir -p "$DIST_DIR"
STAGING_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-release.XXXXXX")
trap 'rm -rf "$STAGING_DIR"' EXIT INT TERM

# Keep the shipped `froth` command name transitional inside Frothy-branded
# release assets.
cp "$BINARY" "$STAGING_DIR/froth"
cat >"$STAGING_DIR/README.txt" <<EOF
Frothy CLI release archive
==========================

Archive name: $(cli_asset_name "$VERSION" "$GOOS" "$GOARCH")
Installed command: froth
Repo-local checkout build: tools/cli/froth-cli

This naming split is intentional during the current transition:
- product, release assets, Homebrew formula, and VS Code surface use Frothy
- the installed CLI command remains \`froth\`

Direct-install reminder:
- place \`froth\` somewhere on PATH
- then run \`froth --version\`
- then run \`froth doctor\`
EOF
ARCHIVE="$DIST_DIR/$(cli_asset_name "$VERSION" "$GOOS" "$GOARCH")"
LC_ALL=C COPYFILE_DISABLE=1 tar -C "$STAGING_DIR" -czf "$ARCHIVE" froth README.txt

printf '%s\n' "$ARCHIVE"
LC_ALL=C COPYFILE_DISABLE=1 tar -tzf "$ARCHIVE"
