#!/usr/bin/env python3
"""Extract the unchanged Genesis Render_Line routine and its renderer macros."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section

BYTE_FLAGS = (
    "ScrollAOn", "ScrollBOn", "VScrollAl", "VScrollAh", "VScrollBl", "VScrollBh",
    "VSpritel", "VSpriteh", "Sprite_Always_Top", "Swap_Scroll_PriorityA",
    "Swap_Scroll_PriorityB", "Swap_Sprite_Priority", "SpriteOn", "PalLock", "PinkBG", "Bits32",
)
WORDS = (
    "H_Cell", "H_Pix", "H_Pix_Begin", "H_Win_Mul", "H_Scroll_Mask", "H_Scroll_CMul",
    "H_Scroll_CMask", "V_Scroll_CMask", "V_Scroll_MMask", "Win_X_Pos", "Win_Y_Pos",
    "ScrA_Addr", "ScrB_Addr", "Win_Addr", "Spr_Addr", "H_Scroll_Addr", "VDP_Current_Line",
    "VDP_Status", "VRam_Flag", "CRam_Flag", "Sprite_Over", "Mode_555",
)


def extract(source):
    constants = section(source, "%define HIGH_B", "section .data align=64")
    rows = section(source, "\tDECL TAB336\n", "\tDECL TAB320\n")
    masks = section(source, "\tMask_N", "\nsection .bss align=64\n")
    macros = section(source, "%macro GET_X_OFFSET 1\n", "\tDECL Render_Line_32X\n")
    state = section(source, "\tDECL Data_Spr\n", "\tDECL _32X_Rend_Mode")
    return (
        '%include "nasmhead.inc"\n' + constants +
        'section .data align=64\n' + rows + 'ALIGN32\n' + masks +
        'section .bss align=64\nDECL VRam\nresb 65536\nDECL CRam\nresb 256\n'
        'global VSRam_Over\nVSRam_Over: resb 32\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\nDECL VDP_Reg\nresd 26\n'
        'DECL Sprite_Struct\nresd 2048\nDECL Sprite_Visible\nresd 256\n' + state +
        'DECL Screen_16X\nresw 80640\n'
        'DECL Palette\nresw 32768\nDECL Palette32\nresd 32768\n'
        'DECL MD_Palette\nresw 256\nDECL MD_Palette32\nresd 256\n' +
        "".join(f"DECL {name}\nresd 1\n" for name in WORDS) +
        "".join(f"DECL {name}\nresb 4\n" for name in BYTE_FLAGS) +
        '\nsection .text align=64\n' + macros +
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
        parser.exit(1, f"VDP render extraction failed: {error}\n")


if __name__ == "__main__":
    main()
