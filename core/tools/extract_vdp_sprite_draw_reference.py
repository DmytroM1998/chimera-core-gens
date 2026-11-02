#!/usr/bin/env python3
"""Extract unchanged sprite selection, traversal, and pixel macros."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    constants = section(source, "%define HIGH_B", "section .data align=64")
    select = section(source, "%macro UPDATE_MASK_SPRITE 1\n", "%macro PUTPIXEL_P0 5\n")
    pixels = section(source, "%macro PUTPIXEL_P0 5\n", "%macro UPDATE_PALETTE 1\n")
    draw = section(source, "%macro RENDER_LINE_SPR 2\n", "%macro RENDER_LINE 2\n")
    state = section(source, "\tDECL Data_Spr\n", "\tDECL _32X_Rend_Mode")
    flags = ("SpriteOn", "VSpritel", "VSpriteh", "Sprite_Always_Top", "Swap_Sprite_Priority")
    return (
        '%include "nasmhead.inc"\n' + constants +
        'section .data\nsprite_cases:\n'
        'dd reference_sprites_draw.normal, reference_sprites_draw.interlace\n'
        'dd reference_sprites_draw.shadow, reference_sprites_draw.interlace_shadow\n'
        'section .bss align=64\nDECL VRam\nresb 65536\nDECL CRam\nresb 256\n'
        'global VSRam_Over\nVSRam_Over: resb 32\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\nDECL VDP_Reg\nresd 26\n'
        'DECL Sprite_Struct\nresd 2048\nDECL Sprite_Visible\nresd 256\n' + state +
        'DECL Screen_16X\nresw 672\nDECL H_Cell\nresd 1\nDECL H_Pix\nresd 1\n'
        'DECL test_row_offset\nresd 1\n'
        'DECL VDP_Current_Line\nresd 1\nDECL VDP_Status\nresd 1\nDECL Sprite_Over\nresd 1\n'
        'DECL Spr_Addr\nresd 1\nDECL VRam_Flag\nresd 1\n' +
        "".join(f"DECL {flag}\nresd 1\n" for flag in flags) +
        select + pixels + draw +
        '\nsection .text\nglobal reference_sprites_draw\nreference_sprites_draw:\n'
        'pushad\ntest byte [VDP_Reg+4],0x40\njz .done\n'
        'mov eax,[VDP_Reg+48]\nand eax,12\nshr eax,2\n'
        'push dword [test_row_offset]\njmp [sprite_cases+eax*4]\n'
        '.normal:\nRENDER_LINE_SPR 0,0\njmp .finish\n'
        '.interlace:\nRENDER_LINE_SPR 1,0\njmp .finish\n'
        '.shadow:\nRENDER_LINE_SPR 0,1\njmp .finish\n'
        '.interlace_shadow:\nRENDER_LINE_SPR 1,1\n'
        '.finish:\nadd esp,4\n.done:\npopad\nret\n'
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
        parser.exit(1, f"VDP sprite-draw extraction failed: {error}\n")


if __name__ == "__main__":
    main()
