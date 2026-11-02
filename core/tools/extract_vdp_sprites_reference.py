#!/usr/bin/env python3
"""Extract unchanged Genesis sprite preparation macros and renderer dispatch."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    macros = section(source, "%macro MAKE_SPRITE_STRUCT 1\n", "%macro UPDATE_MASK_SPRITE 1\n")
    render = section(source, "\tDECL Render_Line\n", "\tDECL Render_Line_32X\n")
    dispatch = section(render, "\t.VDP_Enable\n", "\n\t.Sprite_Struc_OK\n")
    state = section(source, "\tDECL Data_Misc\n", "\tDECL _32X_Rend_Mode")
    return (
        '%include "nasmhead.inc"\nsection .bss align=64\n'
        'DECL VRam\nresb 65536\nDECL CRam\nresb 256\n'
        'global VSRam_Over\nVSRam_Over: resb 32\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\nDECL VDP_Reg\nresd 26\n'
        'DECL VRam_Flag\nresd 1\nDECL Spr_Addr\nresd 1\n'
        'DECL Sprite_Struct\nresd 2048\n' + state + macros +
        '\nsection .text\nglobal reference_sprites_prepare\nreference_sprites_prepare:\n'
        'pushad\ntest dword [VDP_Reg+4],0x40\njz .Sprite_Struc_OK\n' +
        dispatch + '\n.Sprite_Struc_OK:\npopad\nret\n'
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
        parser.exit(1, f"VDP sprite extraction failed: {error}\n")


if __name__ == "__main__":
    main()
