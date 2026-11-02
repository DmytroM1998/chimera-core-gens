#!/usr/bin/env python3
"""Extract unchanged VDP reset, register setters, and IRQ routines."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    marker = "\t;void Int_Ack(void);"
    if source.count(marker) != 1:
        raise ValueError("expected one VDP interrupt block")
    masks = section(source, "\tH_Scroll_Mask_Table:\n", "section .bss align=64")
    state = section(source, "\tDECL VDP_Reg\n", "section .text align=64")
    reset = section(source, "\t;void Reset_VDP(void)", "\terror:\n")
    setters = section(source, "\tTable_Set_Reg:\n", marker)
    irq = source[source.index(marker):]
    return (
        '%include "nasmhead.inc"\nsection .data\n' + masks +
        'section .bss align=64\n'
        'DECL VRam\nresb 65536\nDECL CRam\nresb 256\n'
        'global VSRam_Over\nVSRam_Over: resd 8\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\n' + state +
        'DECL MD_Screen\nresw 80640\nDECL MD_Screen32\nresd 80640\n'
        'DECL MD_Palette\nresw 256\nDECL MD_Palette32\nresd 256\n'
        'DECL Sprite_Struct\nresd 2048\nDECL Sprite_Visible\nresd 256\n'
        'DECL PalLock\nresb 1\nalignb 4\nDECL _main68k_context\nresd 36\n'
        'section .text\nextern reference_interrupt\n'
        'global _main68k_interrupt\n_main68k_interrupt:\n'
        'push ecx\npush edx\npush dword [esp+16]\npush dword [esp+16]\n'
        'call reference_interrupt\nadd esp,8\npop edx\npop ecx\nxor eax,eax\nret\n' +
        reset + setters + irq +
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
        parser.exit(1, f"VDP reset extraction failed: {error}\n")


if __name__ == "__main__":
    main()
