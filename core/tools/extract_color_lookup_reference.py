#!/usr/bin/env python3
"""Extract Recalculate_Palettes with only its MSVC-style cast syntax normalized."""

import argparse
from pathlib import Path

from extract_audio_functions import extract_function


def extract(source):
    function = extract_function(source, "Recalculate_Palettes")
    if function.count("unsigned int (") != 12:
        raise ValueError("unexpected grayscale cast syntax")
    function = function.replace("unsigned int (", "(unsigned int)(")
    return (
        '#include <stdint.h>\nstatic_assert(sizeof(bool) == 1, "Gens bool width");\n'
        'extern "C" {\nuint16_t Palette[32768];\nuint32_t Palette32[32768];\n'
        'int RMax_Level, GMax_Level, BMax_Level, Brightness_Level, Contrast_Level;\n'
        'int Mode_555, Greyscale, Invert_Color;\nbool PinkBG;\n}\n'
        'static uint16_t _32X_Palette_16B[65536], _32X_VDP_CRam[256];\n'
        'static uint16_t _32X_VDP_CRam_Ajusted[256];\n'
        'static uint32_t _32X_Palette_32B[65536], _32X_VDP_CRam_Ajusted32[256];\n' +
        function +
        '\nextern "C" void reference_color_lookup(void) { Recalculate_Palettes(); }\n'
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
        parser.exit(1, f"Color lookup reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
