#!/usr/bin/env bash
# Point this repo's git hooks at tools/git-hooks/, so the tracked hooks
# fire on every commit. Run once after cloning.
set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"
chmod +x tools/git-hooks/*
git config core.hooksPath tools/git-hooks
echo "git hooks path -> tools/git-hooks"
echo "installed: $(ls tools/git-hooks/ | tr '\n' ' ')"
