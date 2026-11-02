#!/usr/bin/env python3
"""Extract unchanged scanline sprite masking and selection macros."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    macro = section(source, "%macro UPDATE_MASK_SPRITE 1\n", "%macro PUTPIXEL_P0 5\n")
    state = section(source, "\tDECL Data_Misc\n", "\tDECL _32X_Rend_Mode")
    return (
        '%include "nasmhead.inc"\nsection .bss align=64\n'
        'DECL Sprite_Struct\nresd 2048\nDECL Sprite_Visible\nresd 256\n' + state +
        'DECL H_Cell\nresd 1\nDECL H_Pix\nresd 1\nDECL VDP_Current_Line\nresd 1\n'
        'DECL VDP_Status\nresd 1\nDECL Sprite_Over\nresd 1\n' + macro +
        '\nsection .text\nglobal reference_sprite_line\nreference_sprite_line:\n'
        'pushad\ntest dword [Sprite_Over],1\njz .unlimited\n'
        'UPDATE_MASK_SPRITE 1\njmp .done\n.unlimited:\nUPDATE_MASK_SPRITE 0\n'
        '.done:\npopad\nret\n'
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
        parser.exit(1, f"VDP sprite-line extraction failed: {error}\n")


if __name__ == "__main__":
    main()
