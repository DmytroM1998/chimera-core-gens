#!/usr/bin/env python3
"""Extract the pinned i386 VDP register setter for differential comparison."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    masks = section(source, "\tH_Scroll_Mask_Table:\n", "section .bss align=64")
    state = section(source, "\tDECL VDP_Reg\n", "\tDECL DMAT_Tmp\n")
    flags = section(source, "\tDECL CRam_Flag\n", "section .text align=64")
    setters = section(source, "\tTable_Set_Reg:\n",
                      "\t;void Int_Ack(void);")
    return (
        '%include "nasmhead.inc"\nsection .data\n' + masks +
        'section .bss align=64\nDECL VRam\nresb 65536\n' + state + flags +
        'global reference_irq_calls\nreference_irq_calls: resd 1\n'
        'section .text\nUpdate_IRQ_Line:\ninc dword [reference_irq_calls]\nret\n' +
        setters + '\nsection .note.GNU-stack noalloc noexec nowrite progbits\n'
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
        parser.exit(1, f"VDP register reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
