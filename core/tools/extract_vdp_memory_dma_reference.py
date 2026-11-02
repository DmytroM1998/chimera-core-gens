#!/usr/bin/env python3
"""Extract unchanged Genesis memory-DMA loops, control decoder, and timing."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    tables = section(source, "\tCD_Table:\n", "\tDECL Genesis_Started\n")
    registers = section(source, "\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n")
    state = section(source, "\tDECL Ctrl\n", "section .text align=64")
    macro = section(source, "%macro DMA_LOOP 2\n", ";***********************************************")
    timing = section(source, "\t;void Update_DMA(void)", "\t;unsigned short Read_VDP_Data(void)")
    commands = section(source, "\t;void Write_VDP_Ctrl(unsigned short Data)",
                       "\t.RAMPRG_To_VRam_Star\n")
    end = section(source, "\t.End_DMA\n", "\tV_RAM_Copy:\n")
    no_dma = section(source, "\tNO_DMA:\n", "\tTable_Set_Reg:\n")
    traps = "".join(
        f".{name}_To_{target}_Star:\nud2\n"
        for name in ("RAMPRG", "RAMWORD2M", "RAMWORD1M0", "RAMWORD1M1", "CELL0", "CELL1")
        for target in ("VRam", "CRam", "VSRam")
    )
    return (
        '%include "nasmhead.inc"\nsection .data\n' + tables +
        'Table_Set_Reg: times 32 dd unsupported_vdp_register\n'
        'section .bss align=64\nDECL Rom_Data\nresb 6291456\n'
        'DECL Ram_68k\nresb 65536\nDECL VRam\nresb 65536\n'
        'DECL CRam\nresb 256\nDECL VSRam\nresb 256\n' + registers + state +
        'DECL Rom_Size\nresd 1\nDECL SegaCD_Started\nresd 1\n'
        'DECL Ram_Word_State\nresd 1\nDECL CPL_M68K\nresd 1\n'
        'global reference_cycle_counter\nreference_cycle_counter: resd 1\n' +
        macro + '\nsection .text\n' + commands + traps + end +
        'V_RAM_Copy:\nud2\n' + no_dma + timing +
        'global _main68k_releaseCycles\n_main68k_releaseCycles:\n'
        'sub [reference_cycle_counter],eax\nret\n'
        'unsupported_vdp_register:\nud2\n'
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
        parser.exit(1, f"VDP memory DMA extraction failed: {error}\n")


if __name__ == "__main__":
    main()
