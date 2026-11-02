#!/usr/bin/env python3
"""Extract unchanged renderer palette macros for an i386 comparison."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    macros = section(source, "%macro UPDATE_PALETTE 1\n", "%macro RENDER_LINE_SCROLL_B 3\n")
    return (
        '%include "nasmhead.inc"\nsection .bss align=64\n'
        'DECL CRam\nresb 256\nDECL Palette\nresw 32768\nDECL Palette32\nresd 32768\n'
        'DECL MD_Palette\nresw 256\nDECL MD_Palette32\nresd 256\n'
        'DECL VDP_Reg\nresd 26\nDECL CRam_Flag\nresd 1\nDECL Mode_555\nresd 1\n'
        'DECL PalLock\nresb 1\nDECL PinkBG\nresb 1\n' + macros +
        '\nsection .text\nglobal reference_palette_update\nreference_palette_update:\n'
        'pushad\ntest byte [CRam_Flag],1\njz .done\n'
        'test byte [VDP_Reg+48],8\njnz .shadow\nUPDATE_PALETTE32 0\njmp .done\n'
        '.shadow:\nUPDATE_PALETTE32 1\n.done:\npopad\nret\n'
        '\nsection .note.GNU-stack noalloc noexec nowrite progbits\n'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        content = extract(args.source.read_text(encoding="latin-1"))
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(content)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"VDP palette extraction failed: {error}\n")


if __name__ == "__main__":
    main()
