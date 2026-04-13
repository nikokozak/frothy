#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT_DIR/tools/release-common.sh"

if [ "$#" -ne 4 ]; then
  printf 'usage: %s <version> <darwin-arm64-sha> <darwin-amd64-sha> <linux-amd64-sha>\n' "${0##*/}" >&2
  exit 1
fi

if [ -z "${HOMEBREW_TAP_TOKEN:-}" ]; then
  printf 'HOMEBREW_TAP_TOKEN is required\n' >&2
  exit 1
fi

VERSION=$(normalize_version "$1")
DARWIN_ARM64_SHA=$2
DARWIN_AMD64_SHA=$3
LINUX_AMD64_SHA=$4

TAP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/frothy-tap.XXXXXX")
trap 'rm -rf "$TAP_DIR"' EXIT INT TERM

git clone "https://x-access-token:${HOMEBREW_TAP_TOKEN}@github.com/${HOMEBREW_TAP_REPO_SLUG}.git" "$TAP_DIR"
git -C "$TAP_DIR" config user.name "github-actions[bot]"
git -C "$TAP_DIR" config user.email "github-actions[bot]@users.noreply.github.com"

"$ROOT_DIR/tools/update-brew-formula.sh" "$VERSION" "$DARWIN_ARM64_SHA" "$DARWIN_AMD64_SHA" "$LINUX_AMD64_SHA" "$TAP_DIR/Formula/frothy.rb"
ruby -c "$TAP_DIR/Formula/frothy.rb"

git -C "$TAP_DIR" add Formula/frothy.rb
if git -C "$TAP_DIR" diff --cached --quiet; then
  printf 'No Homebrew formula changes to commit.\n'
  exit 0
fi

git -C "$TAP_DIR" commit -m "frothy $VERSION"
git -C "$TAP_DIR" push
