#!/usr/bin/env python3
"""Extract pinned VDP read routines and table initialization for comparison."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    registers = section(source, "\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n")
    control = section(source, "\tDECL Ctrl\n", "\tDECL DMAT_Tmp\n")
    status = section(source, "\tDECL VDP_Status\n", "\tDECL CRam_Flag\n")
    error = section(source, "\terror:\n", "\t;void Update_DMA(void)")
    table = section(source, "\t.Loop_HC\n", "\t\tmov dword [VSRam_Over + 28], 0")
    reads = section(source, "\t;unsigned short Read_VDP_Data(void)",
                    "\t;void Write_Byte_VDP_Data(unsigned char Data)")
    return (
        '%include "nasmhead.inc"\n'
        'section .bss align=64\n'
        'DECL VRam\nresb 65536\nDECL CRam\nresb 256\nDECL VSRam\nresb 256\n'
        'DECL H_Counter_Table\nresb 1024\nDECL Cycles_M68K\nresd 1\nDECL CPL_M68K\nresd 1\n' +
        registers + control + status +
        'section .text\nextern reference_vdp_odometer\n'
        'global _main68k_readOdometer\n_main68k_readOdometer:\n'
        'push ecx\npush edx\ncall reference_vdp_odometer\npop edx\npop ecx\nret\n' +
        'global reference_vdp_table_init\nreference_vdp_table_init:\n'
        'push ebx\npush ecx\npush edx\nxor ebx,ebx\n' + table +
        'pop edx\npop ecx\npop ebx\nret\n' + error + reads +
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
        parser.exit(1, f"VDP read reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
