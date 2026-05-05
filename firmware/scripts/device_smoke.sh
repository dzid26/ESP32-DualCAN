#!/usr/bin/env bash
# Flash the firmware, capture serial output for a fixed window, and fail loudly
# if the device crashed or rebooted. On panic, automatically pipes the dump
# through decode_panic.sh.
#
# Usage:
#   ./scripts/device_smoke.sh                    # default env, 30 s window
#   ./scripts/device_smoke.sh -e esp32-c6-debug
#   ./scripts/device_smoke.sh -t 60              # 60 s capture
#   ./scripts/device_smoke.sh --no-flash         # only monitor (already flashed)
#
# Exits non-zero if any of these strings appear in the captured log:
#   "Guru Meditation" "abort()" "rst:" (after the first boot)
#   "assert_func" "CORRUPT HEAP" "Stack canary watchpoint"
#
# Requires: pio in PATH, a device connected on /dev/ttyACM0 (or $DORKY_PORT).

set -euo pipefail

ENV=esp32-c6
TIMEOUT=30
DO_FLASH=1
PORT=${DORKY_PORT:-/dev/ttyACM0}

while [ $# -gt 0 ]; do
  case "$1" in
    -e)         ENV=$2;          shift 2 ;;
    -t)         TIMEOUT=$2;      shift 2 ;;
    -p)         PORT=$2;         shift 2 ;;
    --no-flash) DO_FLASH=0;      shift ;;
    -h|--help)  sed -n '2,18p' "$0"; exit 0 ;;
    *)          echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [ ! -e "$PORT" ]; then
  echo "device port $PORT not found — is the board connected?" >&2
  exit 3
fi

if [ "$DO_FLASH" = 1 ]; then
  echo "==> flashing $ENV"
  pio run -e "$ENV" -t upload --upload-port "$PORT" >/dev/null
fi

LOGFILE=$(mktemp -t dorky-smoke-XXXXXX.log)
trap 'rm -f "$LOGFILE"' EXIT

echo "==> capturing serial output for ${TIMEOUT}s on $PORT"
# Read serial directly via pyserial. pio device monitor breaks when stdin
# isn't a TTY (it tries to set termios on stdin and dies with EINVAL),
# which is exactly what happens in non-interactive scripts and CI.
# Prefer PlatformIO's bundled python — it always has pyserial. Fall back to
# system python3 only if the user explicitly sets PYTHON or the PIO venv is
# missing.
if [ -n "${PYTHON:-}" ]; then
  :
elif [ -x "$HOME/.platformio/penv/bin/python" ]; then
  PYTHON=$HOME/.platformio/penv/bin/python
else
  PYTHON=python3
fi
"$PYTHON" - "$PORT" "$TIMEOUT" >"$LOGFILE" 2>&1 <<'PY' || true
import sys, time, serial
port, timeout = sys.argv[1], float(sys.argv[2])
ser = serial.Serial(port, 115200, timeout=0.2)
end = time.time() + timeout
while time.time() < end:
    try:
        chunk = ser.read(4096)
    except Exception as e:
        print(f"read error: {e}", file=sys.stderr)
        break
    if chunk:
        sys.stdout.write(chunk.decode('utf-8', errors='replace'))
        sys.stdout.flush()
ser.close()
PY

echo "----- captured output (${TIMEOUT}s) -----"
cat "$LOGFILE"
echo "----------------------------------------"

# Panic / crash markers. We deliberately count "rst:" (a CPU reset) as a fault
# *only after* the first boot, because the very first line is always a reset.
PANIC_PATTERNS='Guru Meditation|abort()|assert_func|CORRUPT HEAP|Stack canary watchpoint'

if grep -qE "$PANIC_PATTERNS" "$LOGFILE"; then
  echo
  echo "!!! PANIC DETECTED — decoding backtrace !!!"
  echo
  ./scripts/decode_panic.sh -e "$ENV" <"$LOGFILE" || true
  exit 1
fi

# Multiple resets within the capture window = boot loop.
RST_COUNT=$(grep -cE '^rst:0x[0-9a-fA-F]+' "$LOGFILE" || true)
if [ "${RST_COUNT:-0}" -gt 1 ]; then
  echo "!!! BOOT LOOP DETECTED ($RST_COUNT resets) !!!"
  exit 1
fi

# Sanity: did the firmware print *anything*? An empty buffer means either the
# port was locked, the device is bricked, or it's silent. Any ESP-IDF log line
# (I/W/E/D level prefix with timestamp) counts as proof of life.
if ! grep -qE '^[IWED] \([0-9]+\)' "$LOGFILE"; then
  echo "warning: no ESP-IDF log lines captured — port locked, device silent, or bricked."
  exit 1
fi

echo "OK: no panic, no boot loop, firmware booted."
