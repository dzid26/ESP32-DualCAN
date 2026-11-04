#!/usr/bin/env bash
# Builds a tiny native parse-checker against the bundled Berry sources and
# runs it over every script in firmware/test_scripts/. Catches syntax /
# parse errors in examples without needing a flashed ESP32.
#
# Usage:  ./tools/parse_check_examples.sh           # check test_scripts/
#         ./tools/parse_check_examples.sh path.be   # check specific files

set -euo pipefail

cd "$(dirname "$0")/.."         # firmware/
ROOT=$(pwd)

BERRY_SRC=$ROOT/components/berry/src
BERRY_DEFAULT=$ROOT/components/berry/port
BUILD=$ROOT/.cache/parse_check
mkdir -p "$BUILD"

BIN=$BUILD/parse_check
SRCS=()
while IFS= read -r source; do
  if [ -z "$source" ]; then continue; fi
  if [[ "$source" == *"*"* ]]; then
    for file in "$ROOT"/$source; do
      SRCS+=( "$file" )
    done
  else
    SRCS+=( "$ROOT/$source" )
  fi
done < "$ROOT/tools/parse_check_sources.txt"
SRCS+=( "$ROOT/tools/parse_check.c" )

# Rebuild if the binary is missing or older than any source file.
needs_build=0
if [ ! -x "$BIN" ]; then
  needs_build=1
else
  for s in "${SRCS[@]}"; do
    if [ "$s" -nt "$BIN" ]; then
      needs_build=1
      break
    fi
  done
  if [ "$ROOT/tools/parse_check_sources.txt" -nt "$BIN" ]; then
    needs_build=1
  fi
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
  files=( "$ROOT"/test_scripts/*.be "$ROOT"/test_scripts/manual/*.be )
fi

if [ "${#files[@]}" -eq 0 ]; then
  echo "no .be files to check"; exit 0
fi

"$BIN" "${files[@]}"
