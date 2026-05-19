"""
Ensure git submodules are at their pinned revisions.

Runs `git submodule update --init --recursive` so a `git pull` that bumps a
submodule pin is reflected in the tree before the build starts.

Works both as a PlatformIO pre-build script and as a standalone CLI:
    python firmware/tools/sync_submodules.py
"""

import os
import subprocess

try:
    Import("env")  # noqa: F821 — PlatformIO SCons global
    cwd = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    cwd = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

result = subprocess.run(
    ["git", "submodule", "update", "--init", "--recursive"],
    cwd=cwd,
    capture_output=True,
    text=True,
)

if result.stdout.strip():
    print(result.stdout.strip())
if result.returncode != 0:
    print(f"Warning: git submodule update failed:\n{result.stderr.strip()}")
