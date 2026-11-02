#!/usr/bin/env python3
"""Extract unchanged scroll B pattern traversal and pixel macros."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def wrapper(name, macro):
    cases = "".join(
        f".c{n}:\n{macro} {n & 1},{n >> 2 & 1},{n >> 1 & 1}\njmp .finish\n"
        for n in range(8))
    return (
        f'global {name}\n{name}:\n'
        'pushad\nmov eax,[VDP_Reg+48]\nand eax,12\nshr eax,2\n'
        'mov ebx,[VDP_Reg+44]\nand ebx,4\nor eax,ebx\n'
        f'push dword [test_row_offset]\njmp [{name}_cases+eax*4]\n' + cases +
        '.finish:\nadd esp,4\npopad\nret\n'
    )


def extract(source):
    constants = section(source, "%define HIGH_B", "section .data align=64")
    masks = section(source, "\tMask_N", "\nsection .bss align=64\n")
    offsets = section(source, "%macro GET_X_OFFSET 1\n", "%macro MAKE_SPRITE_STRUCT 1\n")
    pixels = section(source, "%macro PUTPIXEL_P0 5\n", "%macro UPDATE_PALETTE 1\n")
    scroll = section(source, "%macro RENDER_LINE_SCROLL_B 3\n", "%macro RENDER_LINE_SPR 2\n")
    state = section(source, "\tDECL Data_Spr\n", "\tDECL _32X_Rend_Mode")
    flags = ("ScrollBOn", "VScrollBl", "VScrollBh", "Swap_Scroll_PriorityB",
             "ScrollAOn", "VScrollAl", "VScrollAh", "Swap_Scroll_PriorityA",
             "Win_X_Pos", "Win_Y_Pos", "H_Win_Mul", "Win_Addr", "V_Scroll_MMask", "ScrA_Addr")
    tables = "".join(
        f"{name}_cases:\n" + "".join(f"dd {name}.c{n}\n" for n in range(8))
        for name in ("reference_scroll_b", "reference_scroll_a"))
    return (
        '%include "nasmhead.inc"\n' + constants +
        'section .data\n' + tables + 'ALIGN32\n' + masks +
        'section .bss align=64\nDECL VRam\nresb 65536\nDECL CRam\nresb 256\n'
        'global VSRam_Over\nVSRam_Over: resb 32\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\nDECL VDP_Reg\nresd 26\n' + state +
        'DECL Screen_16X\nresw 672\nDECL H_Cell\nresd 1\nDECL H_Pix\nresd 1\n'
        'DECL test_row_offset\nresd 1\nDECL VDP_Current_Line\nresd 1\n'
        'DECL VDP_Status\nresd 1\nDECL H_Scroll_Addr\nresd 1\nDECL ScrB_Addr\nresd 1\n'
        'DECL H_Scroll_Mask\nresd 1\nDECL H_Scroll_CMul\nresd 1\n'
        'DECL H_Scroll_CMask\nresd 1\nDECL V_Scroll_CMask\nresd 1\n' +
        "".join(f"DECL {flag}\nresd 1\n" for flag in flags) +
        offsets + pixels + scroll +
        '\nsection .text\n' + wrapper("reference_scroll_b", "RENDER_LINE_SCROLL_B") +
        wrapper("reference_scroll_a", "RENDER_LINE_SCROLL_A_WIN") +
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
        parser.exit(1, f"VDP scroll extraction failed: {error}\n")


if __name__ == "__main__":
    main()
