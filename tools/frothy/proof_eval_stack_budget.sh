#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)

compile_db=
for candidate in \
  "$repo_root/build/test/host-default/compile_commands.json" \
  "$repo_root/build/compile_commands.json"
do
  if [ -f "$candidate" ]; then
    compile_db=$candidate
    break
  fi
done

if [ -z "$compile_db" ]; then
  echo "error: missing compile_commands.json for host build" >&2
  echo "hint: run 'cmake -S . -B build && cmake --build build' first" >&2
  exit 1
fi

tmpdir=$(mktemp -d)
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT HUP INT TERM

compile_stack_usage() {
  source_file=$1
  object_file=$2
  command_line=$(
    rg -N --no-filename "\"command\": \".*${source_file}\"" "$compile_db" \
      | sed 's/^.*"command": "//; s/",$//; s/\\"/"/g'
  )

  if [ -z "$command_line" ]; then
    echo "error: missing compile command for $source_file in $compile_db" >&2
    exit 1
  fi

  command_line=$(printf '%s' "$command_line" \
    | sed "s# -o [^ ]* -c # -fstack-usage -o $object_file -c #")

  if ! sh -c "$command_line" >/dev/null 2>&1; then
    echo "error: failed to generate stack-usage data for $source_file" >&2
    exit 1
  fi
}

stack_bytes() {
  su_file=$1
  function_name=$2

  bytes=$(
    awk -F '\t' -v fn="$function_name" '
      {
        n = split($1, parts, ":");
        if (n > 0 && parts[n] == fn) {
          print $2;
          found = 1;
          exit;
        }
      }
      END {
        if (!found) {
          exit 1;
        }
      }
    ' "$su_file"
  ) || {
    echo "error: missing stack-usage entry for $function_name in $su_file" >&2
    exit 1
  }

  printf '%s\n' "$bytes"
}

check_budget() {
  su_file=$1
  function_name=$2
  budget=$3

  bytes=$(stack_bytes "$su_file" "$function_name")
  printf '%s %s/%s bytes\n' "$function_name" "$bytes" "$budget"
  if [ "$bytes" -gt "$budget" ]; then
    echo "error: $function_name uses $bytes bytes of stack (budget $budget)" >&2
    exit 1
  fi
}

eval_obj="$tmpdir/frothy_eval_stack.o"
value_obj="$tmpdir/frothy_value_stack.o"
parser_obj="$tmpdir/frothy_parser_stack.o"
shell_obj="$tmpdir/frothy_shell_stack.o"

compile_stack_usage "src/frothy_eval.c" "$eval_obj"
compile_stack_usage "src/frothy_value.c" "$value_obj"
compile_stack_usage "src/frothy_parser.c" "$parser_obj"
compile_stack_usage "src/frothy_shell.c" "$shell_obj"

check_budget "${eval_obj%.o}.su" frothy_eval_node 256
check_budget "${eval_obj%.o}.su" frothy_eval_call 384
check_budget "${eval_obj%.o}.su" frothy_eval_if 128
check_budget "${eval_obj%.o}.su" frothy_eval_while 128
check_budget "${eval_obj%.o}.su" frothy_eval_seq 128
check_budget "${eval_obj%.o}.su" frothy_eval_program 128
check_budget "${value_obj%.o}.su" frothy_runtime_alloc_record_def_from_ir 512
check_budget "${parser_obj%.o}.su" frothy_parse_top_level_internal 512
check_budget "${parser_obj%.o}.su" frothy_parse_in 256
check_budget "${parser_obj%.o}.su" frothy_parse_cond 256
check_budget "${parser_obj%.o}.su" frothy_parse_case 384
check_budget "${shell_obj%.o}.su" frothy_shell_eval_source 256
check_budget "${shell_obj%.o}.su" frothy_shell_run 384
