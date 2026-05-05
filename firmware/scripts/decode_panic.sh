#!/usr/bin/env bash
# Resolve an ESP32-C6 panic backtrace to source lines.
#
# Usage:
#   ./scripts/decode_panic.sh < panic.txt
#   pio device monitor | ./scripts/decode_panic.sh    # live: paste-then-Ctrl-D
#   ./scripts/decode_panic.sh -e <env> < panic.txt    # alt env (default: esp32-c6)
#
# Reads the panic output on stdin, finds the Backtrace line plus MEPC/RA/MTVAL
# registers, and runs riscv32-esp-elf-addr2line against the matching env's
# firmware.elf. Anything not panic-related is ignored.
#
# MCAUSE quick reference (RISC-V): 1=instr access, 2=illegal instr,
# 5=load access, 7=store access, 11=ecall.

set -euo pipefail

ENV=esp32-c6
while getopts "e:h" opt; do
  case "$opt" in
    e) ENV=$OPTARG ;;
    h) sed -n '2,15p' "$0"; exit 0 ;;
    *) exit 2 ;;
  esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ELF=$ROOT/.pio/build/$ENV/firmware.elf
A2L=$HOME/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line

[ -x "$A2L" ] || { echo "addr2line not found at $A2L"; exit 1; }
[ -f "$ELF" ] || { echo "elf not found: $ELF (run 'pio run -e $ENV' first)"; exit 1; }

input=$(cat)

resolve() {
  local label=$1 addr=$2
  [ -z "$addr" ] && return
  local out
  out=$("$A2L" -e "$ELF" -f -p -C "$addr" 2>&1 | sed 's|'"$ROOT/"'||')
  printf '  %-12s %s   %s\n' "$label" "$addr" "$out"
}

# Pull single registers from the dump (RISC-V naming).
mepc=$(  echo "$input" | grep -oE 'MEPC\s*:\s*0x[0-9a-fA-F]+'    | head -1 | grep -oE '0x[0-9a-fA-F]+' || true)
ra=$(    echo "$input" | grep -oE 'RA\s*:\s*0x[0-9a-fA-F]+'      | head -1 | grep -oE '0x[0-9a-fA-F]+' || true)
mtval=$( echo "$input" | grep -oE 'MTVAL\s*:\s*0x[0-9a-fA-F]+'   | head -1 | grep -oE '0x[0-9a-fA-F]+' || true)
mcause=$(echo "$input" | grep -oE 'MCAUSE\s*:\s*0x[0-9a-fA-F]+'  | head -1 | grep -oE '0x[0-9a-fA-F]+' || true)

if [ -n "$mcause$mepc$ra$mtval" ]; then
  echo "Trap registers:"
  [ -n "$mcause" ] && printf '  MCAUSE       %s\n' "$mcause"
  [ -n "$mtval" ]  && printf '  MTVAL        %s    (faulting address for load/store faults)\n' "$mtval"
  [ -n "$mepc" ]   && resolve "MEPC" "$mepc"
  [ -n "$ra" ]     && resolve "RA"   "$ra"
  echo
fi

# Backtrace line: "Backtrace: 0xPC:0xSP 0xPC:0xSP ..."
bt=$(echo "$input" | grep -m1 '^Backtrace:' || true)
if [ -z "$bt" ]; then
  echo "no Backtrace line found in input." >&2
  [ -n "$mepc$ra" ] || exit 1
  exit 0
fi

addrs=$(echo "$bt" | grep -oE '0x[0-9a-fA-F]+:0x[0-9a-fA-F]+' | cut -d: -f1)
echo "Backtrace ($(echo "$addrs" | wc -l | tr -d ' ') frames):"
i=0
for a in $addrs; do
  [ "$a" = "0x00000000" ] && continue
  i=$((i + 1))
  resolve "#$i" "$a"
done
