#!/bin/sh

# Release assets and tap metadata stay Frothy-branded during the transition.
# The release-time `froth` executable name stays separate from those asset names.
RELEASE_REPO_SLUG=${RELEASE_REPO_SLUG:-nikokozak/frothy}
HOMEBREW_TAP_REPO_SLUG=${HOMEBREW_TAP_REPO_SLUG:-nikokozak/homebrew-frothy}
DEFAULT_FIRMWARE_BOARD=${DEFAULT_FIRMWARE_BOARD:-esp32-devkit-v1}

normalize_version() {
  case "$1" in
    v*) printf '%s\n' "${1#v}" ;;
    *) printf '%s\n' "$1" ;;
  esac
}

release_tag() {
  version=$(normalize_version "$1")
  printf 'v%s\n' "$version"
}

cli_asset_name() {
  version=$(normalize_version "$1")
  os=$2
  arch=$3
  printf 'frothy-v%s-%s-%s.tar.gz\n' "$version" "$os" "$arch"
}

firmware_asset_name() {
  version=$(normalize_version "$1")
  board=${2:-$DEFAULT_FIRMWARE_BOARD}
  printf 'frothy-v%s-%s.zip\n' "$version" "$board"
}

checksums_asset_name() {
  version=$(normalize_version "$1")
  printf 'frothy-v%s-checksums.txt\n' "$version"
}

release_download_base() {
  printf 'https://github.com/%s/releases/download/%s\n' "$RELEASE_REPO_SLUG" "$(release_tag "$1")"
}

cli_asset_url() {
  version=$(normalize_version "$1")
  os=$2
  arch=$3
  printf '%s/%s\n' "$(release_download_base "$version")" "$(cli_asset_name "$version" "$os" "$arch")"
}

detect_goos() {
  case "$(uname -s)" in
    Darwin) printf 'darwin\n' ;;
    Linux) printf 'linux\n' ;;
    *)
      printf 'unsupported OS: %s\n' "$(uname -s)" >&2
      return 1
      ;;
  esac
}

detect_goarch() {
  case "$(uname -m)" in
    x86_64|amd64) printf 'amd64\n' ;;
    arm64|aarch64) printf 'arm64\n' ;;
    *)
      printf 'unsupported architecture: %s\n' "$(uname -m)" >&2
      return 1
      ;;
  esac
}
