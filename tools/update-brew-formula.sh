#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT_DIR/tools/release-common.sh"

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  printf 'usage: %s <version> <darwin-arm64-sha> <darwin-amd64-sha> <linux-amd64-sha> [output]\n' "${0##*/}" >&2
  exit 1
fi

VERSION=$(normalize_version "$1")
DARWIN_ARM64_SHA=$2
DARWIN_AMD64_SHA=$3
LINUX_AMD64_SHA=$4
OUTPUT=${5:-Formula/frothy.rb}

DARWIN_ARM64_ASSET=$(cli_asset_name "$VERSION" darwin arm64)
DARWIN_AMD64_ASSET=$(cli_asset_name "$VERSION" darwin amd64)
LINUX_AMD64_ASSET=$(cli_asset_name "$VERSION" linux amd64)

mkdir -p "$(dirname "$OUTPUT")"
TMP_FILE=$(mktemp "${TMPDIR:-/tmp}/frothy-formula.XXXXXX")
trap 'rm -f "$TMP_FILE"' EXIT INT TERM

# Generate a Frothy-branded formula that installs the Frothy-owned CLI
# executable.
cat >"$TMP_FILE" <<EOF
class Frothy < Formula
  desc "A live lexical language for programmable devices"
  homepage "https://github.com/nikokozak/frothy"
  version "${VERSION}"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/${RELEASE_REPO_SLUG}/releases/download/v#{version}/${DARWIN_ARM64_ASSET}"
      sha256 "${DARWIN_ARM64_SHA}"
    end
    on_intel do
      url "https://github.com/${RELEASE_REPO_SLUG}/releases/download/v#{version}/${DARWIN_AMD64_ASSET}"
      sha256 "${DARWIN_AMD64_SHA}"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/${RELEASE_REPO_SLUG}/releases/download/v#{version}/${LINUX_AMD64_ASSET}"
      sha256 "${LINUX_AMD64_SHA}"
    end
  end

  def install
    bin.install "frothy"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/frothy --version")
  end
end
EOF

mv "$TMP_FILE" "$OUTPUT"
