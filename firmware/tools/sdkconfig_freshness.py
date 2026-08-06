"""
Pre-build hook: keep sdkconfig.<env> in sync with sdkconfig.defaults*.

PIO's espressif32 framework treats sdkconfig.<env> as user config — seeded
from sdkconfig.defaults only when missing, then preserved so `pio run -t
menuconfig` edits survive. That means edits to sdkconfig.defaults aren't
picked up until the cached file is deleted. Same goes for any overlay
(sdkconfig.defaults.release, etc.).

This script does two things:

  1. Auto-staleness check on every build — if the aggregated content of
     the sdkconfig.defaults* files differs from the last-seen content,
     delete the cached sdkconfig.<env> and the matching
     .pio/build/<env>/config/ dir so the next build regenerates from the
     fresh defaults.

     Content-hash based (not mtime), so a defaults edit is picked up even
     if it pidn't bump the file timestamp "newer" than the cached config.

  2. Extends `pio run -t clean` (the VSCode trash button) to also delete
     sdkconfig.<env>, so cleaning does what you'd expect.
"""

Import("env")  # noqa: F821 — PlatformIO SCons global

import glob
import hashlib
import os
import shutil


PIOENV       = env["PIOENV"]
PROJECT_DIR  = env["PROJECT_DIR"]
CACHED       = os.path.join(PROJECT_DIR, f"sdkconfig.{PIOENV}")
DEFAULTS     = sorted(glob.glob(os.path.join(PROJECT_DIR, "sdkconfig.defaults*")))
CONFIG_DIR   = os.path.join(env.subst("$BUILD_DIR"), "config")
HASH_FILE    = CACHED + ".defaults.sha256"


def _defaults_digest():
    """Stable SHA-256 over all sdkconfig.defaults* basenames + contents."""
    h = hashlib.sha256()
    for f in DEFAULTS:
        with open(f, "rb") as fh:
            h.update(os.path.basename(f).encode("utf-8"))
            h.update(b"\x00")
            h.update(fh.read())
        h.update(b"\x00")
    return h.hexdigest()


def _read_stored():
    try:
        with open(HASH_FILE, "r") as fh:
            return fh.read().strip()
    except OSError:
        return None


def _write_stored(digest):
    with open(HASH_FILE, "w") as fh:
        fh.write(digest)


def _remove():
    if os.path.exists(CACHED):
        print(f"[sdkconfig] removing {CACHED}")
        os.remove(CACHED)
    if os.path.isdir(CONFIG_DIR):
        print(f"[sdkconfig] removing {CONFIG_DIR}")
        shutil.rmtree(CONFIG_DIR)
    if os.path.exists(HASH_FILE):
        os.remove(HASH_FILE)


# (1) Content-hash staleness check
if DEFAULTS:
    digest = _defaults_digest()
    stored = _read_stored()
    if stored is None or stored != digest:
        print("[sdkconfig] defaults content changed, regenerating")
        _remove()
        _write_stored(digest)

# (2) Extend `pio run -t clean` to also nuke sdkconfig.<env>
if env.GetOption("clean"):
    _remove()
