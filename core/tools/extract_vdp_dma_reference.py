#!/usr/bin/env python3
"""Extract the pinned DMA timing table and unchanged Update_DMA routine."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    table = section(source, "\tDMA_Timing_Table:\n", "\tDECL Genesis_Started\n")
    update = section(source, "\t;void Update_DMA(void)", "\t;unsigned short Read_VDP_Data(void)")
    globals_ = (
        "DMAT_Length", "DMAT_Type", "VDP_Status", "VDP_Current_Line",
        "VDP_Num_Vis_Lines", "CPL_M68K"
    )
    return (
        '%include "nasmhead.inc"\nsection .data\n' + table +
        'section .bss align=64\nDECL VDP_Reg\nresd 26\n' +
        "".join(f"DECL {name}\nresd 1\n" for name in globals_) +
        'section .text\n' + update +
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
        parser.exit(1, f"VDP DMA timing extraction failed: {error}\n")


if __name__ == "__main__":
    main()
