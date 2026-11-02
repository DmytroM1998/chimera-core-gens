#!/usr/bin/env python3
"""Extract the pinned Gens PCM routines for an external comparison."""

import argparse
from pathlib import Path

from extract_audio_functions import extract_function


def extract_cpp(text):
    return "\n\n".join(extract_function(text, name) for name in (
        "lowpass", "Write_Sound_Stereo", "Write_Sound_Mono",
        "Dump_Sound_Stereo", "Dump_Sound_Mono",
    )) + "\n"


def extract_mmx(text):
    marker = "\t; void Write_Sound_Mono_MMX(int *Left, int *Right, short *Dest, int length)"
    if text.count(marker) != 1:
        raise ValueError("expected unique MMX output marker")
    body = text[text.index(marker):]
    if body.count("\tDECL Write_Sound_Mono_MMX\n") != 1 or \
            body.count("\tDECL Write_Sound_Stereo_MMX\n") != 1 or body.count("\tDECL ") != 2:
        raise ValueError("unexpected MMX output function set")
    return ('%include "nasmhead.inc"\nsection .text\n' + body +
            '\nsection .note.GNU-stack noalloc noexec nowrite progbits\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="pinned Gens src directory")
    parser.add_argument("output", type=Path, help="new directory for extracted routines")
    args = parser.parse_args()
    try:
        cpp = extract_cpp((args.source / "G_dsound.cpp").read_text(encoding="latin-1"))
        mmx = extract_mmx((args.source / "Misc.asm").read_text(encoding="latin-1"))
        args.output.mkdir()
        (args.output / "pcm_reference.inc").write_text(cpp, encoding="utf-8", newline="\n")
        (args.output / "pcm_reference.asm").write_text(mmx, encoding="utf-8", newline="\n")
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"PCM reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
