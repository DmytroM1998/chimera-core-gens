#!/usr/bin/env python3
"""Extract unchanged eight-pixel plane/sprite compositor macros."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    constants = section(source, "%define HIGH_B", "section .data align=64")
    macros = section(source, "%macro PUTPIXEL_P0 5\n", "%macro UPDATE_PALETTE 1\n")
    render = section(source, "\tDECL Render_Line\n", "\tDECL Render_Line_32X\n")
    resolve = section(render, "\t.Genesis_Loop\n", "\t\tadd esp, byte 4\n")
    table = "".join(f"dd reference_pixel_row.case_{i}\n" for i in range(24))
    cases = []
    for layer in range(3):
        for priority in range(2):
            for shadow in range(2):
                for flip in range(2):
                    index = layer * 8 + priority * 4 + shadow * 2 + flip
                    if layer == 2:
                        macro = "PUTLINE_SPRITE_FLIP" if flip else "PUTLINE_SPRITE"
                        args = f"{'PRIO_B' if priority else '0'}, {shadow}"
                    else:
                        macro = f"PUTLINE_{'FLIP_' if flip else ''}P{priority}"
                        args = f"{layer}, {shadow}"
                    cases.append(f".case_{index}:\n{macro} {args}\njmp .done\n")
    globals_ = (
        "test_pattern", "test_palette", "test_offset", "test_variant",
        "test_enabled", "test_priority_enabled", "VDP_Status",
        "VScrollAl", "VScrollAh", "VScrollBl", "VScrollBh", "VSpritel", "VSpriteh",
        "test_background", "H_Pix_Begin"
    )
    return (
        '%include "nasmhead.inc"\n' + constants +
        'section .data\npixel_cases:\n' + table +
        'section .bss align=64\nDECL Screen_16X\nresw 384\nDECL CRam\nresb 256\n' +
        "".join(f"DECL {name}\nresd 1\n" for name in globals_) +
        'DECL ScrollAOn\nresb 1\nDECL ScrollBOn\nresb 1\nDECL SpriteOn\nresb 1\n' +
        macros + '\nsection .text\nglobal reference_pixel_row\nreference_pixel_row:\n'
        'pushad\nmov ebx,[test_pattern]\nmov edx,[test_palette]\nmov ebp,[test_offset]\n'
        'mov eax,[test_enabled]\nmov [ScrollAOn],al\nmov [ScrollBOn],al\nmov [SpriteOn],al\n'
        'mov eax,[test_priority_enabled]\n'
        'mov [VScrollAl],eax\nmov [VScrollAh],eax\nmov [VScrollBl],eax\nmov [VScrollBh],eax\n'
        'mov [VSpritel],eax\nmov [VSpriteh],eax\n'
        'mov eax,[test_variant]\ncmp eax,16\njb .plane\nsub ebp,8\n'
        '.plane:\npush dword 0\njmp [pixel_cases+eax*4]\n' +
        "".join(cases) + '.done:\nadd esp,4\npopad\nret\n' +
        'global reference_pixel_resolve\nreference_pixel_resolve:\npushad\n'
        'mov ecx,160\nsub ecx,[H_Pix_Begin]\nmov edi,[test_offset]\n'
        'lea edi,[Screen_16X+edi*2]\nmov esi,CRam\nmov ebp,[test_background]\n'
        'and ebp,63\n' + resolve + 'popad\nret\n'
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
        parser.exit(1, f"VDP pixel extraction failed: {error}\n")


if __name__ == "__main__":
    main()
