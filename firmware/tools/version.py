"""
Pre-build script: inject firmware version from git into the build.

Format (from `git describe --tags --dirty --always`):
  On a tag:              0.2.1-rc1
  Commits past a tag:    0.2.1-rc1-3-gabcdef
  Uncommitted changes:   0.2.1-rc1-dirty
  No tags at all:        gabcdef

Strips a leading 'v' from the tag name so the version stored in
DORKY_FIRMWARE_VERSION is always bare (no 'v' prefix).

Writes src/version.h only when the string changes so that incremental
builds only recompile files that include it, not every translation unit.
"""

Import("env")  # noqa: F821 — PlatformIO SCons global

import os
import subprocess

try:
    cwd = env["PROJECT_DIR"]
    # Exact tag match only — any commits on top or dirty falls through to hash
    tag = subprocess.check_output(
        ["git", "describe", "--tags", "--exact-match"],
        stderr=subprocess.DEVNULL, cwd=cwd,
    ).decode().strip().lstrip("v")
    version = tag
except subprocess.CalledProcessError:
    # Not on a clean tag — use short hash, append -dirty if needed
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=cwd,
    ).decode().strip()
    dirty = subprocess.call(
        ["git", "diff", "--quiet", "--", ":(exclude)firmware/sdkconfig.*"],
        cwd=cwd,
    ) != 0
    version = sha + ("-dirty" if dirty else "")
except Exception:
    version = "unknown"

print(f"Firmware version: {version}")

version_h = os.path.join(env["PROJECT_DIR"], "src", "version.h")
content = f'#pragma once\n#define DORKY_FIRMWARE_VERSION "{version}"\n'

try:
    with open(version_h) as f:
        existing = f.read()
except FileNotFoundError:
    existing = ""

if existing != content:
    with open(version_h, "w") as f:
        f.write(content)
