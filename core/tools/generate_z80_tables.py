#!/usr/bin/env python3
"""Generate Z80 dispatch tables from the pinned z80.asm opcode tables.

Every table entry is classified into an instruction family with small
arguments. Names that the C port does not implement yet map to UNIMPLEMENTED,
which makes the executor fail explicitly instead of guessing.
"""

import argparse
import re
from pathlib import Path

TABLES = ("OP_Table", "CB_Table", "DD_Table", "ED_Table", "FD_Table", "DDCB_Table", "FDCB_Table")
REGISTERS = {"B": 0, "C": 1, "D": 2, "E": 3, "H": 4, "L": 5, "A": 6,
             "hIX": 7, "lIX": 8, "hIY": 9, "lIY": 10}
INDEX = {"IX": 0, "IY": 1}
PREFIXES = {"PREFIXE_CB": "PREFIX_CB", "PREFIXE_ED": "PREFIX_ED", "PREFIXE_DD": "PREFIX_DD",
            "PREFIXE_FD": "PREFIX_FD", "PREFIXE_DDCB": "PREFIX_DDCB",
            "PREFIXE_FDCB": "PREFIX_FDCB"}
FAMILIES = (
    "UNIMPLEMENTED", "NOP", "PREFIX_CB", "PREFIX_ED", "PREFIX_DD", "PREFIX_FD", "PREFIX_DDCB",
    "PREFIX_FDCB", "LD_R_R", "LD_R_N", "LD_R_MHL", "LD_MHL_R", "LD_MHL_N", "LD_R_MXYD",
    "LD_MXYD_R", "LD_MXYD_N", "LD_A_MBC", "LD_A_MDE", "LD_A_MNN", "LD_MBC_A", "LD_MDE_A",
    "LD_MNN_A", "LD_A_I", "LD_A_R", "LD_I_A", "LD_R_A", "LD_RR_NN", "LD_RR_MNN", "LD_MNN_RR",
    "LD_SP_RR", "PUSH", "POP", "EX_DE_HL", "EX_AF_AF2", "EXX", "EX_MSP",
    "ALU_R", "ALU_N", "ALU_MHL", "ALU_MXYD", "INCDEC_R", "INCDEC_MHL", "INCDEC_MXYD",
    "INCDEC_RR", "DAA", "CPL", "NEG", "CCF", "SCF", "HALT", "DI", "EI", "IM", "ADD16",
    "ADC_HL", "SBC_HL", "ROT_A",
    "ROT_R", "ROT_MHL", "ROT_MXYD", "BIT_R", "BIT_MHL", "BIT_MXYD", "SETRES_R", "SETRES_MHL",
    "SETRES_MXYD", "JP", "JPCC", "JR", "JRCC", "JP_RR", "DJNZ", "CALL", "CALLCC", "RET", "RETCC",
    "RETN", "RST", "IN_N", "IN_RBC", "INX", "INXR", "OUT_N", "OUT_RBC", "OUTX", "OUTXR", "LDX",
    "LDXR", "CPX", "CPXR", "RLD", "RRD",
)
ROTATES = ("RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL")
ROT_NAMES = "|".join(ROTATES)
CONDITIONS = {"Z": 0x40, "C": 0x01, "P": 0x04, "S": 0x80}
ALU = {"ADD": 0, "ADC": 1, "SUB": 2, "SBC": 3, "AND": 4, "XOR": 5, "OR": 6, "CP": 7}
ALU_NAMES = "|".join(ALU)
PAIRS = {"BC": 0, "DE": 1, "HL": 2, "SP": 3, "IX": 4, "IY": 5, "AF": 6}
REG = "(A|B|C|D|E|H|L|hIX|lIX|hIY|lIY)"
RULES = (
    (re.compile(rf"^LD_{REG}_{REG}$"), "LD_R_R", lambda m: (REGISTERS[m[1]], REGISTERS[m[2]])),
    (re.compile(rf"^LD_{REG}_N$"), "LD_R_N", lambda m: (REGISTERS[m[1]], 0)),
    (re.compile(r"^LD_(A|B|C|D|E|H|L)_mHL$"), "LD_R_MHL", lambda m: (REGISTERS[m[1]], 0)),
    (re.compile(r"^LD_mHL_(A|B|C|D|E|H|L)$"), "LD_MHL_R", lambda m: (REGISTERS[m[1]], 0)),
    (re.compile(r"^LD_mHL_N$"), "LD_MHL_N", lambda m: (0, 0)),
    (re.compile(r"^LD_(A|B|C|D|E|H|L)_m(IX|IY)d$"), "LD_R_MXYD",
     lambda m: (REGISTERS[m[1]], INDEX[m[2]])),
    (re.compile(r"^LD_m(IX|IY)d_(A|B|C|D|E|H|L)$"), "LD_MXYD_R",
     lambda m: (REGISTERS[m[2]], INDEX[m[1]])),
    (re.compile(r"^LD_m(IX|IY)d_N$"), "LD_MXYD_N", lambda m: (0, INDEX[m[1]])),
    (re.compile(r"^LD_A_mBC$"), "LD_A_MBC", lambda m: (0, 0)),
    (re.compile(r"^LD_A_mDE$"), "LD_A_MDE", lambda m: (0, 0)),
    (re.compile(r"^LD_A_mNN$"), "LD_A_MNN", lambda m: (0, 0)),
    (re.compile(r"^LD_mBC_A$"), "LD_MBC_A", lambda m: (0, 0)),
    (re.compile(r"^LD_mDE_A$"), "LD_MDE_A", lambda m: (0, 0)),
    (re.compile(r"^LD_mNN_A$"), "LD_MNN_A", lambda m: (0, 0)),
    (re.compile(r"^LD_A_I$"), "LD_A_I", lambda m: (0, 0)),
    (re.compile(r"^LD_A_R$"), "LD_A_R", lambda m: (0, 0)),
    (re.compile(r"^LD_I_A$"), "LD_I_A", lambda m: (0, 0)),
    (re.compile(r"^LD_R_A$"), "LD_R_A", lambda m: (0, 0)),
    (re.compile(r"^NOP$"), "NOP", lambda m: (0, 0)),
    (re.compile(r"^LD_(BC|DE|HL|SP|IX|IY)_NN$"), "LD_RR_NN", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^LD_(HL|IX|IY)_mNN$"), "LD_RR_MNN", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^LD_(BC|DE|SP)_mNN$"), "LD_RR_MNN", lambda m: (PAIRS[m[1]], 1)),
    (re.compile(r"^LD2_HL_mNN$"), "LD_RR_MNN", lambda m: (PAIRS["HL"], 1)),
    (re.compile(r"^LD_mNN_(HL|IX|IY)$"), "LD_MNN_RR", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^LD_mNN_(BC|DE|SP)$"), "LD_MNN_RR", lambda m: (PAIRS[m[1]], 1)),
    (re.compile(r"^LD2_mNN_HL$"), "LD_MNN_RR", lambda m: (PAIRS["HL"], 1)),
    (re.compile(r"^LD_SP_(HL|IX|IY)$"), "LD_SP_RR", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^PUSH_(AF|BC|DE|HL|IX|IY)$"), "PUSH", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^POP_(AF|BC|DE|HL|IX|IY)$"), "POP", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^EX_DE_HL$"), "EX_DE_HL", lambda m: (0, 0)),
    (re.compile(r"^EX_AF_AF2$"), "EX_AF_AF2", lambda m: (0, 0)),
    (re.compile(r"^EXX$"), "EXX", lambda m: (0, 0)),
    (re.compile(r"^EX_mSP_(HL|IX|IY)$"), "EX_MSP", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(rf"^({ALU_NAMES})_{REG}$"), "ALU_R", lambda m: (ALU[m[1]], REGISTERS[m[2]])),
    (re.compile(rf"^({ALU_NAMES})_N$"), "ALU_N", lambda m: (ALU[m[1]], 0)),
    (re.compile(rf"^({ALU_NAMES})_mHL$"), "ALU_MHL", lambda m: (ALU[m[1]], 0)),
    (re.compile(rf"^({ALU_NAMES})_m(IX|IY)d$"), "ALU_MXYD", lambda m: (ALU[m[1]], INDEX[m[2]])),
    (re.compile(rf"^(INC|DEC)_{REG}$"), "INCDEC_R", lambda m: (REGISTERS[m[2]], m[1] == "DEC")),
    (re.compile(r"^(INC|DEC)_mHL$"), "INCDEC_MHL", lambda m: (0, m[1] == "DEC")),
    (re.compile(r"^(INC|DEC)_m(IX|IY)d$"), "INCDEC_MXYD", lambda m: (INDEX[m[2]], m[1] == "DEC")),
    (re.compile(r"^(INC|DEC)_(BC|DE|HL|IX|IY|SP)$"), "INCDEC_RR",
     lambda m: (PAIRS[m[2]], m[1] == "DEC")),
    (re.compile(r"^DAA$"), "DAA", lambda m: (0, 0)),
    (re.compile(r"^CPL$"), "CPL", lambda m: (0, 0)),
    (re.compile(r"^NEG$"), "NEG", lambda m: (0, 0)),
    (re.compile(r"^CCF$"), "CCF", lambda m: (0, 0)),
    (re.compile(r"^SCF$"), "SCF", lambda m: (0, 0)),
    (re.compile(r"^HALT$"), "HALT", lambda m: (0, 0)),
    (re.compile(r"^DI$"), "DI", lambda m: (0, 0)),
    (re.compile(r"^EI$"), "EI", lambda m: (0, 0)),
    (re.compile(r"^IM([012])$"), "IM", lambda m: (int(m[1]), 0)),
    (re.compile(r"^ADD_(HL|IX|IY)_(BC|DE|HL|IX|IY|SP)$"), "ADD16",
     lambda m: (PAIRS[m[1]], PAIRS[m[2]])),
    (re.compile(r"^ADC_HL_(BC|DE|HL|SP)$"), "ADC_HL", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^SBC_HL_(BC|DE|HL|SP)$"), "SBC_HL", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^(RLCA|RLA|RRCA|RRA)$"), "ROT_A",
     lambda m: (("RLCA", "RLA", "RRCA", "RRA").index(m[1]), 0)),
    (re.compile(rf"^({ROT_NAMES})_(A|B|C|D|E|H|L)$"), "ROT_R",
     lambda m: (ROTATES.index(m[1]), REGISTERS[m[2]])),
    (re.compile(rf"^({ROT_NAMES})_mHL$"), "ROT_MHL", lambda m: (ROTATES.index(m[1]), 0)),
    (re.compile(rf"^({ROT_NAMES})_m(IX|IY)d(?:_(A|B|C|D|E|H|L))?$"), "ROT_MXYD",
     lambda m: (ROTATES.index(m[1]) | INDEX[m[2]] << 3, REGISTERS[m[3]] + 1 if m[3] else 0)),
    (re.compile(r"^BIT([0-7])_(A|B|C|D|E|H|L)$"), "BIT_R", lambda m: (int(m[1]), REGISTERS[m[2]])),
    (re.compile(r"^BIT([0-7])_mHL$"), "BIT_MHL", lambda m: (int(m[1]), 0)),
    (re.compile(r"^BIT([0-7])_m(IX|IY)d$"), "BIT_MXYD", lambda m: (int(m[1]), INDEX[m[2]])),
    (re.compile(r"^(SET|RES)([0-7])_(A|B|C|D|E|H|L)$"), "SETRES_R",
     lambda m: (int(m[2]) | (m[1] == "SET") << 3, REGISTERS[m[3]])),
    (re.compile(r"^(SET|RES)([0-7])_mHL$"), "SETRES_MHL",
     lambda m: (int(m[2]) | (m[1] == "SET") << 3, 0)),
    (re.compile(r"^(SET|RES)([0-7])_m(IX|IY)d(?:_(A|B|C|D|E|H|L))?$"), "SETRES_MXYD",
     lambda m: (int(m[2]) | (m[1] == "SET") << 3 | INDEX[m[3]] << 4,
                REGISTERS[m[4]] + 1 if m[4] else 0)),
    (re.compile(r"^JP_NN$"), "JP", lambda m: (0, 0)),
    (re.compile(r"^JP(N?)(Z|C|P|S)_NN$"), "JPCC", lambda m: (CONDITIONS[m[2]], m[1] == "N")),
    (re.compile(r"^JR_N$"), "JR", lambda m: (0, 0)),
    (re.compile(r"^JR(N?)(Z|C)_N$"), "JRCC", lambda m: (CONDITIONS[m[2]], m[1] == "N")),
    (re.compile(r"^JP_(HL|IX|IY)$"), "JP_RR", lambda m: (PAIRS[m[1]], 0)),
    (re.compile(r"^DJNZ$"), "DJNZ", lambda m: (0, 0)),
    (re.compile(r"^CALL_NN$"), "CALL", lambda m: (0, 0)),
    (re.compile(r"^CALL(N?)(Z|C|P|S)_NN$"), "CALLCC", lambda m: (CONDITIONS[m[2]], m[1] == "N")),
    (re.compile(r"^RET$"), "RET", lambda m: (0, 0)),
    (re.compile(r"^RET(N?)(Z|C|P|S)$"), "RETCC", lambda m: (CONDITIONS[m[2]], m[1] == "N")),
    (re.compile(r"^RET(I|N)$"), "RETN", lambda m: (0, 0)),
    (re.compile(r"^RST$"), "RST", lambda m: (0, 0)),
    (re.compile(r"^IN_mN$"), "IN_N", lambda m: (0, 0)),
    (re.compile(r"^IN_(A|B|C|D|E|H|L|F)_mBC$"), "IN_RBC",
     lambda m: (REGISTERS[m[1]] if m[1] != "F" else 0xff, 0)),
    (re.compile(r"^IN(I|D)$"), "INX", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^IN(I|D)R$"), "INXR", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^OUT_mN$"), "OUT_N", lambda m: (0, 0)),
    (re.compile(r"^OUT_mBC_(A|B|C|D|E|H|L|0)$"), "OUT_RBC", lambda m: (0, 0)),
    (re.compile(r"^OUT(I|D)$"), "OUTX", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^OT(I|D)R$"), "OUTXR", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^LD(I|D)$"), "LDX", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^LD(I|D)R$"), "LDXR", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^CP(I|D)$"), "CPX", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^CP(I|D)R$"), "CPXR", lambda m: (m[1] == "D", 0)),
    (re.compile(r"^RLD$"), "RLD", lambda m: (0, 0)),
    (re.compile(r"^RRD$"), "RRD", lambda m: (0, 0)),
)


def classify(name):
    if name in PREFIXES:
        return PREFIXES[name], 0, 0
    if not name.startswith("Z80I_"):
        raise ValueError(f"unexpected table entry {name!r}")
    short = name[5:]
    for pattern, family, arguments in RULES:
        match = pattern.match(short)
        if match:
            return (family, *arguments(match))
    return "UNIMPLEMENTED", 0, 0


def parse_tables(source):
    tables = {}
    for name in TABLES:
        marker = f"\t{name}\n"
        if source.count(marker) != 1:
            raise ValueError(f"expected unique table {name}")
        entries = []
        for line in source[source.index(marker) + len(marker):].splitlines():
            line = line.split(";")[0].strip()
            if not line.startswith("dd "):
                break
            entries.extend(item.strip() for item in line[3:].split(","))
        if len(entries) != 256:
            raise ValueError(f"table {name} has {len(entries)} entries")
        tables[name] = entries
    return tables


def parse_data(source, name, directive, count, convert):
    marker = f"\t{name}\n"
    if source.count(marker) != 1:
        raise ValueError(f"expected unique table {name}")
    values = []
    for line in source[source.index(marker) + len(marker):].splitlines():
        line = line.split(";")[0].strip()
        if not line:
            continue
        if not line.startswith(directive + " "):
            break
        values.extend(convert(item.strip()) for item in line[len(directive) + 1:].split(","))
    if len(values) != count:
        raise ValueError(f"table {name} has {len(values)} entries")
    return values


def format_values(values, width, per_line):
    lines = []
    for start in range(0, len(values), per_line):
        lines.append("    " + ", ".join(f"0x{v:0{width}x}" for v in values[start:start + per_line]))
    return ",\n".join(lines)


def generate(source):
    tables = parse_tables(source)
    output = ["/* Generated by tools/generate_z80_tables.py from the pinned z80.asm. */\n",
              '#include "gens_z80_tables.h"\n\n']
    for name in TABLES:
        output.append(f"const GensZ80Op gens_z80_{name.lower()}[256] = {{\n")
        for index, entry in enumerate(tables[name]):
            family, first, second = classify(entry)
            output.append(f"    /* {index:02X} {entry} */ "
                          f"{{GENS_Z80_{family}, {int(first)}, {int(second)}}},\n")
        output.append("};\n\n")
    for name, directive, count, convert, width, ctype in (
            ("INC_Table", "db", 256, int, 2, "uint8_t"),
            ("DEC_Table", "db", 256, int, 2, "uint8_t"),
            ("DAA_Table", "dw", 2048, lambda text: int(text.rstrip("hH"), 16), 4, "uint16_t")):
        values = parse_data(source, name, directive, count, convert)
        output.append(f"const {ctype} gens_z80_{name.lower()}[{count}] = {{\n"
                      f"{format_values(values, width, 8)}\n}};\n\n")
    return "".join(output)


def header():
    members = ",\n".join(f"    GENS_Z80_{name}" for name in FAMILIES)
    return (
        "/* Generated by tools/generate_z80_tables.py. */\n"
        "#ifndef CHIMERA_GENS_Z80_TABLES_H\n#define CHIMERA_GENS_Z80_TABLES_H\n\n"
        "#include <stdint.h>\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
        f"enum GensZ80Family {{\n{members}\n}};\n\n"
        "enum GensZ80Register {\n"
        "    GENS_Z80_B, GENS_Z80_C, GENS_Z80_D, GENS_Z80_E, GENS_Z80_H, GENS_Z80_L, GENS_Z80_A,\n"
        "    GENS_Z80_IXH, GENS_Z80_IXL, GENS_Z80_IYH, GENS_Z80_IYL\n};\n\n"
        "enum GensZ80Pair {\n"
        "    GENS_Z80_PAIR_BC, GENS_Z80_PAIR_DE, GENS_Z80_PAIR_HL, GENS_Z80_PAIR_SP,\n"
        "    GENS_Z80_PAIR_IX, GENS_Z80_PAIR_IY, GENS_Z80_PAIR_AF\n};\n\n"
        "typedef struct GensZ80Op {\n    uint8_t family, first, second;\n} GensZ80Op;\n\n"
        + "".join(f"extern const GensZ80Op gens_z80_{name.lower()}[256];\n" for name in TABLES) +
        "extern const uint8_t gens_z80_inc_table[256];\n"
        "extern const uint8_t gens_z80_dec_table[256];\n"
        "extern const uint16_t gens_z80_daa_table[2048];\n"
        "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        source = args.source.read_text(encoding="latin-1")
        (args.directory / "gens_z80_tables.c").write_text(generate(source), newline="\n")
        (args.directory / "gens_z80_tables.h").write_text(header(), newline="\n")
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Z80 table generation failed: {error}\n")


if __name__ == "__main__":
    main()
