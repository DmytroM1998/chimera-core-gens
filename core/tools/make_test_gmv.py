#!/usr/bin/env python3
"""Write a Gens Movie TESTA file from an input script of tools/headless_input.py.

The layout follows Gens-rr movie.cpp: a 64-byte header, then three active-low bytes a frame.
It is a test fixture writer, not a movie converter.
"""

import argparse
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script")
    parser.add_argument("output")
    parser.add_argument("--p1", choices=["3", "6"], default="6")
    parser.add_argument("--p2", choices=["3", "6"], default="6")
    parser.add_argument("--flags", type=lambda text: int(text, 0), default=0,
                        help="header byte 22: 0x80 PAL, 0x20 Team Player")
    args = parser.parse_args()
    header = bytearray(64)
    header[0:16] = b"Gens Movie TESTA"
    header[16:20] = struct.pack("<I", 0)
    header[20] = ord(args.p1)
    header[21] = ord(args.p2)
    header[22] = args.flags
    header[24:44] = b"chimera-core-gens   "
    out = bytearray(header)
    with open(args.script) as script:
        for line in script:
            parts = line.split()
            p1 = int(parts[0], 16) if parts and parts[0] != "R" else 0
            p2 = int(parts[1], 16) if len(parts) > 1 else 0
            if args.flags & 0x20:
                extra = 0
            else:
                extra = (p1 >> 8) & 0xF | ((p2 >> 8) & 0xF) << 4
            out += bytes([~p1 & 0xFF, ~p2 & 0xFF, ~extra & 0xFF])
    with open(args.output, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()