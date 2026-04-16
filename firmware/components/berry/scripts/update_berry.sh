#!/usr/bin/env bash
# Update vendored Berry sources from the git submodule.
#
# Prerequisites:
#   git submodule update --init firmware/components/berry/berry
#
# What it does:
#   1. Copies berry/src/*.{c,h} into src/ (minus be_repl, be_filelib).
#   2. Runs coc to regenerate the generate/ headers using our berry_conf.h.
#   3. Copies the upstream LICENSE.
#
# After running, review the diff and commit.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMP_DIR="$(dirname "$SCRIPT_DIR")"
SUBMOD="$COMP_DIR/berry"

if [ ! -f "$SUBMOD/src/berry.h" ]; then
    echo "Error: submodule not initialised. Run:"
    echo "  git submodule update --init firmware/components/berry/berry"
    exit 1
fi

echo "Copying sources from submodule..."
rm -f "$COMP_DIR"/src/be_*.c "$COMP_DIR"/src/be_*.h "$COMP_DIR"/src/berry.h
cp "$SUBMOD"/src/*.c "$SUBMOD"/src/*.h "$COMP_DIR/src/"
rm -f "$COMP_DIR/src/be_repl.c" "$COMP_DIR/src/be_repl.h" "$COMP_DIR/src/be_filelib.c"

echo "Running coc..."
python3 "$SUBMOD/tools/coc/coc" \
    -o "$COMP_DIR/generate" \
    "$COMP_DIR/src" "$COMP_DIR/port" \
    -c "$COMP_DIR/port/berry_conf.h"

cp "$SUBMOD/LICENSE" "$COMP_DIR/LICENSE.berry"

echo "Done. Submodule HEAD: $(git -C "$SUBMOD" rev-parse --short HEAD)"
echo "Review changes with: git diff firmware/components/berry/"
