#!/usr/bin/env python3
"""Print linker aliases between underscored and plain symbol names in ELF objects.

Gens' assembly was written for Windows, where C names carry a leading underscore. On ELF the
C compiler adds none, so "_hook_pc" in the assembly and "hook_pc" in C name the same object.
For every undefined symbol whose name with or without one leading underscore is defined in
another object, this prints a linker script assignment "<undefined> = <defined>;". Give the
output file to the linker as an input file (an implicit linker script). Non-ELF objects are
skipped.
"""

import struct
import sys


def symbols(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        return set(), set()
    is64 = data[4] == 2
    if is64:
        shoff, = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    else:
        shoff, = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = []
    for i in range(shnum):
        base = shoff + i * shentsize
        if is64:
            name, kind, _, _, offset, size, link, _, _, entsize = struct.unpack_from("<IIQQQQIIQQ", data, base)
        else:
            name, kind, _, _, offset, size, link, _, _, entsize = struct.unpack_from("<IIIIIIIIII", data, base)
        sections.append((kind, offset, size, link, entsize))
    defined, undefined = set(), set()
    for kind, offset, size, link, entsize in sections:
        if kind != 2 or entsize == 0:
            continue
        strings = sections[link]
        for i in range(size // entsize):
            base = offset + i * entsize
            if is64:
                name, info, _, shndx = struct.unpack_from("<IBBH", data, base)
            else:
                name, _, _, info, _, shndx = struct.unpack_from("<IIIBBH", data, base)
            if info >> 4 == 0:
                continue
            end = data.index(b"\0", strings[1] + name)
            text = data[strings[1] + name:end].decode("latin-1")
            if not text:
                continue
            (undefined if shndx == 0 else defined).add(text)
    return defined, undefined


def main():
    defined, undefined = set(), set()
    for path in sys.argv[1:]:
        d, u = symbols(path)
        defined |= d
        undefined |= u
    for name in sorted(undefined - defined):
        if name.startswith("_") and name[1:] in defined:
            print(f"{name} = {name[1:]};")
        elif "_" + name in defined:
            print(f"{name} = _{name};")
    return 0


if __name__ == "__main__":
    sys.exit(main())