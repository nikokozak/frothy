#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION_FILE="$ROOT_DIR/VERSION"
PART=${1-}

case "$PART" in
  major|minor|patch) ;;
  *)
    printf 'usage: %s <major|minor|patch>\n' "${0##*/}" >&2
    exit 1
    ;;
esac

if [ -n "$(cd "$ROOT_DIR" && git status --short)" ]; then
  printf 'working tree must be clean before version bump\n' >&2
  exit 1
fi

OLD_VERSION=$(cat "$VERSION_FILE")
if ! printf '%s\n' "$OLD_VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  printf 'invalid version: %s\n' "$OLD_VERSION" >&2
  exit 1
fi

IFS=. read -r MAJOR MINOR PATCH <<EOF
$OLD_VERSION
EOF

case "$PART" in
  major)
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
    ;;
  minor)
    MINOR=$((MINOR + 1))
    PATCH=0
    ;;
  patch)
    PATCH=$((PATCH + 1))
    ;;
esac

NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
printf '%s' "$NEW_VERSION" > "$VERSION_FILE"

"$ROOT_DIR/tools/version-propagate.sh"

(cd "$ROOT_DIR" && git add \
  VERSION \
  CMakeLists.txt \
  targets/esp-idf/main/CMakeLists.txt)
(cd "$ROOT_DIR" && git commit -m "release: v$NEW_VERSION")
(cd "$ROOT_DIR" && git tag "v$NEW_VERSION")

printf 'Bumped %s -> %s. Tagged v%s. Push with: git push && git push --tags\n' \
  "$OLD_VERSION" "$NEW_VERSION" "$NEW_VERSION"
