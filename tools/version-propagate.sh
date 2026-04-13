#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION_FILE="$ROOT_DIR/VERSION"

if [ ! -f "$VERSION_FILE" ]; then
  printf 'missing VERSION file\n' >&2
  exit 1
fi

VERSION=$(cat "$VERSION_FILE")
if ! printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  printf 'invalid version: %s\n' "$VERSION" >&2
  exit 1
fi

propagated=no

sed_inplace() {
  file=$1
  shift
  tmp=$(mktemp "${file}.tmp.XXXXXX")
  sed "$@" "$file" > "$tmp"
  if cmp -s "$file" "$tmp"; then
    rm -f "$tmp"
    printf 'unchanged %s\n' "${file#$ROOT_DIR/}"
    return
  fi
  mv "$tmp" "$file"
  printf 'updated %s\n' "${file#$ROOT_DIR/}"
  propagated=yes
}

sed_inplace \
  "$ROOT_DIR/CMakeLists.txt" \
  "s/set(FROTH_VERSION \"[0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*\" CACHE STRING/set(FROTH_VERSION \"$VERSION\" CACHE STRING/"

sed_inplace \
  "$ROOT_DIR/targets/esp-idf/main/CMakeLists.txt" \
  "s/FROTH_VERSION=\"[0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*\"/FROTH_VERSION=\"$VERSION\"/"

(cd "$ROOT_DIR" && make sdk-payload)
printf 'generated tools/cli/internal/sdk payload\n'

if [ "$propagated" = no ]; then
  printf 'version already in sync: %s\n' "$VERSION"
else
  printf 'propagated version %s\n' "$VERSION"
fi
