#!/usr/bin/env python3
"""Extract patched Gens sound-update functions for the isolated C test."""

import argparse
from pathlib import Path
import re
import sys


def extract_function(source, name):
    pattern = re.compile(r"(?m)^(?:static\s+)?(?:void|int)\s+" + re.escape(name) +
                         r"\s*\([^;{}]*\)(?:\s|//[^\n]*|/\*[\s\S]*?\*/)*\{")
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one definition of {name}")
    start = matches[0].start()
    position = matches[0].end() - 1
    depth = 0
    state = "normal"
    while position < len(source):
        char = source[position]
        following = source[position + 1] if position + 1 < len(source) else ""
        if state == "normal":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and following == "/":
                state = "line"
                position += 1
            elif char == "/" and following == "*":
                state = "block"
                position += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[start:position + 1]
                if depth < 0:
                    break
        elif state in ("string", "char"):
            if char == "\\":
                position += 1
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "normal"
        elif state == "line" and char == "\n":
            state = "normal"
        elif state == "block" and char == "*" and following == "/":
            state = "normal"
            position += 1
        position += 1
    raise ValueError(f"unterminated function: {name}")


def extract_patched(source_dir):
    functions = []
    for filename, name in (("psg.c", "PSG_Special_Update"),
                           ("ym2612.c", "YM2612_Special_Update")):
        source = (source_dir / "Gens-rr" / "src" / filename).read_text(encoding="latin-1")
        function = extract_function(source, name)
        if function.count("gens_audio_next_offset(") != 1 or \
                "Sound_Extrapol[VDP_Current_Line + 1]" in function:
            raise ValueError(f"{name} does not have the expected audio patch")
        functions.append(function)
    return "\n\n".join(functions) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="disposable patched Gens source directory")
    parser.add_argument("output", type=Path, help="new include file for the isolated test")
    args = parser.parse_args()
    try:
        content = extract_patched(args.source)
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(content)
    except (OSError, ValueError) as error:
        print(f"Audio test extraction failed: {error}", file=sys.stderr)
        return 1
    print(f"Extracted patched audio functions into {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
