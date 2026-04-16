#!/usr/bin/env python3
"""Convert a DBC text file to the Dorky Commander binary DBC format.

Usage:
    python3 dbc2bin.py input.dbc output.bin [--bus 0]

The binary format matches dbc_types.h and can be loaded directly by the
firmware's dbc_load(). It can also serve as a reference implementation
for the browser-side DBC compiler.
"""

import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

DBC_MAGIC = b"DBC\0"
DBC_VERSION = 1

# Signal flags (must match dbc_types.h)
SIG_SIGNED      = 1 << 0
SIG_BIG_ENDIAN  = 1 << 1
SIG_MUX_NONE    = 0 << 2
SIG_MUX_SELECTOR = 1 << 2
SIG_MUX_MUXED   = 2 << 2


@dataclass
class Signal:
    name: str
    start_bit: int
    bit_length: int
    is_big_endian: bool
    is_signed: bool
    scale: float
    offset: float
    mux_type: int = SIG_MUX_NONE  # SIG_MUX_*
    mux_value: int = 0


@dataclass
class Message:
    id: int
    name: str
    dlc: int
    signals: list = field(default_factory=list)


def parse_dbc(text: str) -> list[Message]:
    messages = []
    current_msg = None

    # Match message lines: BO_ <id> <name>: <dlc> <sender>
    msg_re = re.compile(r"^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+)")
    # Match signal lines: SG_ <name> [mux_indicator] : <start>|<len>@<endian><sign> (<scale>,<offset>) [<min>|<max>] "<unit>" <receivers>
    sig_re = re.compile(
        r'^\s+SG_\s+(\w+)\s+'
        r'(M|m\d+)?\s*:\s*'
        r'(\d+)\|(\d+)@([01])([+-])\s*'
        r'\(([^,]+),([^)]+)\)\s*'
        r'\[([^|]+)\|([^\]]+)\]\s*'
        r'"([^"]*)"\s*'
        r'(.+)'
    )

    for line in text.splitlines():
        m = msg_re.match(line)
        if m:
            current_msg = Message(
                id=int(m.group(1)),
                name=m.group(2),
                dlc=int(m.group(3)),
            )
            messages.append(current_msg)
            continue

        m = sig_re.match(line)
        if m and current_msg is not None:
            mux_indicator = m.group(2) or ""
            if mux_indicator == "M":
                mux_type = SIG_MUX_SELECTOR
                mux_value = 0
            elif mux_indicator.startswith("m"):
                mux_type = SIG_MUX_MUXED
                mux_value = int(mux_indicator[1:])
            else:
                mux_type = SIG_MUX_NONE
                mux_value = 0

            sig = Signal(
                name=m.group(1),
                start_bit=int(m.group(3)),
                bit_length=int(m.group(4)),
                is_big_endian=(m.group(5) == "0"),
                is_signed=(m.group(6) == "-"),
                scale=float(m.group(7)),
                offset=float(m.group(8)),
                mux_type=mux_type,
                mux_value=mux_value,
            )
            current_msg.signals.append(sig)

    # Sort by ID for binary search on the firmware side
    messages.sort(key=lambda m: m.id)
    return messages


def compile_binary(messages: list[Message], bus_id: int = 0) -> bytes:
    # Build string table
    strings = {}  # name -> offset
    str_buf = bytearray()

    def intern(s: str) -> int:
        if s in strings:
            return strings[s]
        off = len(str_buf)
        strings[s] = off
        str_buf.extend(s.encode("utf-8"))
        str_buf.append(0)
        return off

    # Intern all names
    for msg in messages:
        intern(msg.name)
        for sig in msg.signals:
            intern(sig.name)

    # Build signal array (flat, all messages concatenated)
    sig_data = bytearray()
    sig_index = 0
    for msg in messages:
        msg._sig_start = sig_index
        for sig in msg.signals:
            flags = 0
            if sig.is_signed:
                flags |= SIG_SIGNED
            if sig.is_big_endian:
                flags |= SIG_BIG_ENDIAN
            flags |= sig.mux_type
            sig_data += struct.pack(
                "<HBBBBHff",
                intern(sig.name),
                sig.start_bit,
                sig.bit_length,
                flags,
                sig.mux_value,
                0,  # _pad
                sig.scale,
                sig.offset,
            )
            sig_index += 1

    total_sigs = sig_index

    # Build message array
    msg_data = bytearray()
    for msg in messages:
        msg_data += struct.pack(
            "<IHBBHH",
            msg.id,
            intern(msg.name),
            msg.dlc,
            len(msg.signals),
            msg._sig_start,
            0,  # _pad
        )

    # Header
    hdr_size = 28
    msgs_offset = hdr_size
    sigs_offset = msgs_offset + len(msg_data)
    strs_offset = sigs_offset + len(sig_data)

    header = struct.pack(
        "<4sBBHHHIIII",
        DBC_MAGIC,
        DBC_VERSION,
        bus_id,
        len(messages),
        total_sigs,
        0,  # _reserved
        msgs_offset,
        sigs_offset,
        strs_offset,
        len(str_buf),
    )

    return bytes(header + msg_data + sig_data + str_buf)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.dbc output.bin [--bus N]")
        sys.exit(1)

    dbc_path = sys.argv[1]
    out_path = sys.argv[2]
    bus_id = 0
    if "--bus" in sys.argv:
        bus_id = int(sys.argv[sys.argv.index("--bus") + 1])

    text = Path(dbc_path).read_text(encoding="utf-8", errors="replace")
    messages = parse_dbc(text)

    total_sigs = sum(len(m.signals) for m in messages)
    print(f"Parsed {len(messages)} messages, {total_sigs} signals")

    blob = compile_binary(messages, bus_id)
    Path(out_path).write_bytes(blob)
    print(f"Wrote {len(blob)} bytes to {out_path}")


if __name__ == "__main__":
    main()
