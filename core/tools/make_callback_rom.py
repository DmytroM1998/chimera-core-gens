#!/usr/bin/env python3
"""Write a self-authored Genesis ROM for the memory-callback witness."""

import argparse
from pathlib import Path
import struct


def image():
    rom = bytearray(0x400)
    struct.pack_into(">II", rom, 0, 0xFFFF00, 0x200)
    struct.pack_into(">I", rom, 0x80, 0x280)  # TRAP #0
    rom[0x100:0x110] = b"SEGA GENESIS    "
    rom[0x1F0] = ord("U")
    words = [
        0x7001,                          # 200: MOVEQ #1,D0
        0x207C, 0x00FF, 0x0100,          # 202: MOVEA.L #$FF0100,A0
        0x10BC, 0x0080,                  # 208: MOVE.B #$80,(A0)
        0x317C, 0x1234, 0x0002,          # 20C: MOVE.W #$1234,2(A0)
        0x217C, 0x89AB, 0xCDEF, 0x0004,  # 212: MOVE.L #$89ABCDEF,4(A0)
        0x1210,                          # 21A: MOVE.B (A0),D1
        0x3428, 0x0002,                  # 21C: MOVE.W 2(A0),D2
        0x2628, 0x0004,                  # 220: MOVE.L 4(A0),D3
        0x227C, 0x00FF, 0x0110,          # 224: MOVEA.L #$FF0110,A1
        0x2303,                          # 22A: MOVE.L D3,-(A1)
        0x4A01,                          # 22C: TST.B D1
        0x4E71,                          # 22E: NOP
        0x1839, 0x00A1, 0x0003,          # 230: MOVE.B $A10003,D4
        0x4E40,                          # 236: TRAP #0
        0x247C, 0x00A1, 0x000C,          # 238: MOVEA.L #$A1000C,A2
        0x2503,                          # 23E: MOVE.L D3,-(A2) (device writes)
        0x2A39, 0x0000, 0x0100,          # 240: MOVE.L $100,D5 (ROM read)
        0x6000, 0xFFB8,                  # 246: BRA.W $200
    ]
    struct.pack_into(">" + "H" * len(words), rom, 0x200, *words)
    struct.pack_into(">H", rom, 0x280, 0x4E73)  # RTE
    return bytes(rom)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.output.open("xb") as f:
        f.write(image())


if __name__ == "__main__":
    main()
