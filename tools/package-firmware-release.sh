#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT_DIR/tools/release-common.sh"

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  printf 'usage: %s <build-dir> <version> [output]\n' "${0##*/}" >&2
  exit 1
fi

BUILD_DIR=$1
VERSION=$(normalize_version "$2")
OUTPUT=${3:-"$ROOT_DIR/dist/$(firmware_asset_name "$VERSION")"}
MANIFEST_PATH="$BUILD_DIR/flasher_args.json"

if [ ! -f "$MANIFEST_PATH" ]; then
  printf 'missing flasher_args.json: %s\n' "$MANIFEST_PATH" >&2
  exit 1
fi

case "$OUTPUT" in
  /*) ;;
  *) OUTPUT="$(pwd)/$OUTPUT" ;;
esac

mkdir -p "$(dirname "$OUTPUT")"
FILE_LIST=$(mktemp "${TMPDIR:-/tmp}/froth-firmware-files.XXXXXX")
trap 'rm -f "$FILE_LIST"' EXIT INT TERM

if ! command -v go >/dev/null 2>&1; then
  printf 'go is required to read %s without Python\n' "$MANIFEST_PATH" >&2
  exit 1
fi

go run "$ROOT_DIR/tools/firmware_manifest_files.go" "$MANIFEST_PATH" > "$FILE_LIST"

while IFS= read -r relpath; do
  if [ ! -f "$BUILD_DIR/$relpath" ]; then
    printf 'missing firmware artifact referenced by flasher_args.json: %s\n' "$relpath" >&2
    exit 1
  fi
done < "$FILE_LIST"

(cd "$BUILD_DIR" && zip -r "$OUTPUT" -@ < "$FILE_LIST")

printf '%s\n' "$OUTPUT"
