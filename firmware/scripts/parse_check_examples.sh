#!/usr/bin/env bash
# Builds a tiny native parse-checker against the bundled Berry sources and
# runs it over every script in firmware/scripts_examples/. Catches syntax /
# parse errors in examples without needing a flashed ESP32.
#
# Usage:  ./scripts/parse_check_examples.sh           # check examples/
#         ./scripts/parse_check_examples.sh path.be   # check specific files

set -euo pipefail

cd "$(dirname "$0")/.."         # firmware/
ROOT=$(pwd)

BERRY_SRC=$ROOT/components/berry/berry/src
BERRY_DEFAULT=$ROOT/components/berry/berry/default
BUILD=$ROOT/.cache/parse_check
mkdir -p "$BUILD"

BIN=$BUILD/parse_check
SRCS=( "$BERRY_SRC"/*.c "$BERRY_DEFAULT"/be_port.c "$BERRY_DEFAULT"/be_modtab.c "$ROOT/scripts/parse_check.c" )

# Rebuild if the binary is missing or older than any source file.
needs_build=0
if [ ! -x "$BIN" ]; then
  needs_build=1
else
  for s in "${SRCS[@]}"; do
    [ "$s" -nt "$BIN" ] && needs_build=1 && break
  done
fi

if [ "$needs_build" = "1" ]; then
  echo "building parse_check..."
  gcc -std=c99 -O1 -w \
    -I "$BERRY_SRC" -I "$BERRY_DEFAULT" \
    "${SRCS[@]}" -o "$BIN" -lm
fi

if [ "$#" -gt 0 ]; then
  files=("$@")
else
  shopt -s nullglob
  files=( "$ROOT"/scripts_examples/*.be )
fi

if [ "${#files[@]}" -eq 0 ]; then
  echo "no .be files to check"; exit 0
fi

"$BIN" "${files[@]}"
