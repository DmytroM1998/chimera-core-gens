#!/usr/bin/env python3
"""Extract the pinned Genesis-only 68000 dispatch for an external reference."""

import argparse
from pathlib import Path


def section(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f"expected unique source markers: {start!r}, {end!r}")
    first = source.index(start)
    last = source.index(end)
    if first >= last:
        raise ValueError(f"source markers out of order: {start!r}, {end!r}")
    return source[first:last]


def extract(source):
    tables = section(source, "\tDECL Genesis_M68K_Read_Byte_Table",
                     "\t; Sega CD Default Jump Table")
    current = section(source, "\tDECL M68K_Read_Byte_Table", "section .bss align=64")
    memory = section(source, "\tDECL Ram_68k", "section .text align=64")
    init = section(source, "\t.Genesis\n", "\t._32X\n")
    reads = section(source, "\t;unsigned char M68K_RB", "\t;void M68K_WBC")
    writes = section(source, "\t;void M68K_WB(unsigned int Adr, unsigned char Data)",
                     '%include "Mem_M68K_CD.inc"')
    return (
        '%include "nasmhead.inc"\n'
        '%define ALIGNB64 alignb 64\n'
        '%define CYCLE_FOR_TAKE_Z80_BUS_GENESIS 16\n'
        'extern Controller_1_Counter, Controller_1_Delay, Controller_1_State, Controller_1_COM\n'
        'extern Controller_2_Counter, Controller_2_Delay, Controller_2_State, Controller_2_COM\n'
        'extern M_Z80, Z80_ReadB_Table, Z80_WriteB_Table\n'
        'extern Read_VDP_Data, Read_VDP_Status, Read_VDP_V_Counter, Read_VDP_H_Counter\n'
        'extern Write_Byte_VDP_Data, Write_Word_VDP_Data, Write_VDP_Ctrl\n'
        'extern RD_Controller_1, RD_Controller_2, WR_Controller_1, WR_Controller_2\n'
        'extern _main68k_readOdometer, z80_Reset, z80_Exec, z80_Set_Odo\n'
        'extern _YM2612_Reset, _PSG_Write, _Fix_Codes\n'
        'section .data align=64\n' + tables + current +
        'section .bss align=64\n' + memory +
        'section .text align=64\n'
        'global reference_init_genesis\nreference_init_genesis:\n'
        '\tpush eax\n\tpush ebx\n\tmov ebx, 15\n' + init +
        reads + writes +
        '\nsection .note.GNU-stack noalloc noexec nowrite progbits\n'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = extract(args.source.read_text(encoding="ascii"))
        with args.output.open("x", encoding="ascii", newline="\n") as output:
            output.write(result)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"genesis bus reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
