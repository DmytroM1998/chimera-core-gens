#!/usr/bin/env python3
"""Extract pinned VDP control decoding and VRAM-copy DMA for comparison."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    table = section(source, "\tCD_Table:\n", "\tDMA_Timing_Table:\n")
    state = section(source, "\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n")
    control = section(source, "\tDECL Ctrl\n", "section .text align=64")
    commands = section(source, "\t;void Write_VDP_Ctrl(unsigned short Data)",
                       "\t\tand eax, byte 3")
    copy = section(source, "\tV_RAM_Copy:\n", "\tTable_Set_Reg:\n")
    return (
        '%include "nasmhead.inc"\nsection .data\n' + table +
        'Table_Set_Reg: times 32 dd unsupported_vdp_register\n'
        'section .bss align=64\nDECL VRam\nresb 65536\n' + state + control +
        'section .text\n' + commands +
        'unsupported_vdp_memory:\nud2\nunsupported_vdp_register:\nud2\n' + copy +
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
        parser.exit(1, f"VDP copy reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
