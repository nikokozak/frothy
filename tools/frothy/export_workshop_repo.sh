#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
workshop_dir="$repo_root/workshop"
workshop_file="$workshop_dir/starter.frothy"
legacy_pong_file="$workshop_dir/pong.frothy"

usage() {
  cat <<'EOF'
usage: export_workshop_repo.sh [write|check]

write  regenerate workshop/starter.frothy from the canonical starter template
check  fail if workshop/starter.frothy has drifted from the canonical starter template
EOF
}

render_export() {
  cat <<'EOF'
\ -- Frothy workshop starter ------------------------------------
\ Send this file to your board, then type: my.run:
\ Click the joystick to stop. Edit, re-send, run again.

my.x is 5
my.y is 3

to my.setup [
  matrix.init:;
  matrix.brightness!: 1
]

to my.update [
  \ Read inputs and change state here.
  nil
]

to my.draw [
  grid.clear:;
  \ Draw your scene here.
  grid.set: my.x, my.y, true;
  grid.show:
]

to my.frame [
  my.update:;
  my.draw:;
  ms: 42
]

to my.run [
  my.setup:;
  while (not joy.click?:) [
    my.frame:
  ]
]
EOF
}

write_export() {
  tmp_file=$(mktemp "${TMPDIR:-/tmp}/frothy-workshop-starter.XXXXXX")
  trap 'rm -f "$tmp_file"' EXIT INT TERM
  mkdir -p "$workshop_dir"
  render_export > "$tmp_file"
  mv "$tmp_file" "$workshop_file"
}

check_export() {
  tmp_file=$(mktemp "${TMPDIR:-/tmp}/frothy-workshop-starter.XXXXXX")
  trap 'rm -f "$tmp_file"' EXIT INT TERM
  render_export > "$tmp_file"

  if [ -f "$legacy_pong_file" ]; then
    printf 'stale workshop Pong export remains: %s\n' "$legacy_pong_file" >&2
    printf 'remove it and use workshop/starter.frothy\n' >&2
    exit 1
  fi

  if [ ! -f "$workshop_file" ]; then
    printf 'missing workshop starter: %s\n' "$workshop_file" >&2
    printf 'run: sh tools/frothy/export_workshop_repo.sh write\n' >&2
    exit 1
  fi

  if ! cmp -s "$tmp_file" "$workshop_file"; then
    printf 'workshop starter drift: %s\n' "$workshop_file" >&2
    diff -u "$workshop_file" "$tmp_file" >&2 || true
    printf 'run: sh tools/frothy/export_workshop_repo.sh write\n' >&2
    exit 1
  fi
}

mode=${1:-write}
case "$mode" in
  write)
    write_export
    ;;
  check)
    check_export
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    usage >&2
    exit 1
    ;;
esac
