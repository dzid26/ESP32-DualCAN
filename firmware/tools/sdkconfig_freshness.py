"""
Pre-build hook: keep sdkconfig.<env> in sync with sdkconfig.defaults*.

PIO's espressif32 framework treats sdkconfig.<env> as user config — seeded
from sdkconfig.defaults only when missing, then preserved so `pio run -t
menuconfig` edits survive. That means edits to sdkconfig.defaults aren't
picked up until the cached file is deleted. Same goes for any overlay
(sdkconfig.defaults.release, etc.).

This script does two things:

  1. Auto-staleness check on every build — if any sdkconfig.defaults*
     file is newer than the cached sdkconfig.<env>, delete the cached
     file and the matching .pio/build/<env>/config/ dir so the next
     build regenerates from fresh defaults.

  2. Extends `pio run -t clean` (the VSCode trash button) to also delete
     sdkconfig.<env>, so cleaning does what you'd expect.
"""

Import("env")  # noqa: F821 — PlatformIO SCons global

import glob
import os
import shutil


PIOENV       = env["PIOENV"]
PROJECT_DIR  = env["PROJECT_DIR"]
CACHED       = os.path.join(PROJECT_DIR, f"sdkconfig.{PIOENV}")
DEFAULTS     = glob.glob(os.path.join(PROJECT_DIR, "sdkconfig.defaults*"))
CONFIG_DIR   = os.path.join(env.subst("$BUILD_DIR"), "config")


def _remove():
    if os.path.exists(CACHED):
        print(f"[sdkconfig] removing {CACHED}")
        os.remove(CACHED)
    if os.path.isdir(CONFIG_DIR):
        print(f"[sdkconfig] removing {CONFIG_DIR}")
        shutil.rmtree(CONFIG_DIR)


# (1) Auto-staleness check
if os.path.exists(CACHED) and DEFAULTS:
    newest_default = max(os.path.getmtime(f) for f in DEFAULTS)
    if newest_default > os.path.getmtime(CACHED):
        print(f"[sdkconfig] defaults newer than cached, regenerating")
        _remove()

# (2) Extend `pio run -t clean` to also nuke sdkconfig.<env>
if env.GetOption("clean"):
    _remove()
