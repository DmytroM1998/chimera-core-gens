#!/usr/bin/env python3
"""Extract unchanged Genesis screen conversion for an isolated C++ reference."""

import argparse
from pathlib import Path

from extract_audio_functions import extract_function
from extract_genesis_bus_reference import section


def extract(source, header):
    function = extract_function(source, "Render_MD_Screen")
    registers = section(header, "struct Reg_VDP_Type {", "\n// XXX:")
    return (
        '#include <stdint.h>\n' + registers +
        '\nstatic_assert(sizeof(Reg_VDP_Type) == 104 && sizeof(bool) == 1, "Gens reference layout");\n'
        'static Reg_VDP_Type VDP_Reg;\nextern "C" {\n'
        'uint16_t Screen_16X[80640], MD_Screen[80640], Palette[32768];\n'
        'uint32_t MD_Screen32[80640], Palette32[32768], TAB336[240];\n'
        'int VDP_Current_Line, VDP_Num_Vis_Lines;\nbool FakeVDPScreen;\n}\n' +
        function +
        '\nextern "C" void reference_video_mode(unsigned int value) { VDP_Reg.Set4=value; }\n'
        'extern "C" void reference_video_convert(void) { Render_MD_Screen(); }\n'
        'extern "C" void reference_video_render_screen(void) {\n'
        'FakeVDPScreen=false;\nRender_MD_Screen();\n}\n'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        content = extract(args.source.read_text(encoding="latin-1"),
                          args.source.with_name("vdp_io.h").read_text(encoding="latin-1"))
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(content)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"VDP video reference extraction failed: {error}\n")


if __name__ == "__main__":
    main()
