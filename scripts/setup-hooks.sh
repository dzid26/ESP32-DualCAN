#!/usr/bin/env bash
# Point this repo's git hooks at scripts/git-hooks/, so the tracked hooks
# fire on every commit. Run once after cloning.
set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"
chmod +x scripts/git-hooks/*
git config core.hooksPath scripts/git-hooks
echo "git hooks path -> scripts/git-hooks"
echo "installed: $(ls scripts/git-hooks/ | tr '\n' ' ')"
