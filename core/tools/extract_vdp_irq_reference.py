#!/usr/bin/env python3
"""Extract unchanged VDP IRQ update and acknowledge routines for comparison."""

import argparse
from pathlib import Path

from extract_genesis_bus_reference import section


def extract(source):
    marker = "\t;void Int_Ack(void);"
    if source.count(marker) != 1:
        raise ValueError("expected one VDP interrupt routine block")
    registers = section(source, "\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n")
    routines = source[source.index(marker):]
    return (
        '%include "nasmhead.inc"\nsection .bss align=64\n' + registers +
        'DECL VDP_Int\nresd 1\nDECL _main68k_context\nresd 36\n'
        'section .text\nextern reference_interrupt\n'
        'global _main68k_interrupt\n_main68k_interrupt:\njmp reference_interrupt\n'
        'global reference_irq_ack\nreference_irq_ack:\n'
        'mov eax,[esp+4]\ncall Int_Ack\nret\n' + routines +
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
        parser.exit(1, f"VDP IRQ reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
