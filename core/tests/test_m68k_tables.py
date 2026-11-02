import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_m68k_tables", ROOT / "tools" / "generate_m68k_tables.py"
)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
STAR = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "Starscream" / "Main68k" / "Star.c"
TABLES = ROOT / "waterbox" / "gens_m68k_tables.c"
HEADER = ROOT / "waterbox" / "gens_m68k_tables.h"


def committed_runs():
    text = TABLES.read_text()
    body = text[text.index("gens_m68k_runs["):]
    return [(int(r), int(c)) for r, c in re.findall(r"\{(\d+), (\d+)\}", body)]


def committed_routines():
    return re.findall(r"/\* ([0-9A-F]{4}) \*/ \{GENS_M68K_(\w+),", TABLES.read_text())


def decode(opcode):
    position = 0
    for routine, count in committed_runs():
        if opcode < position + count:
            return committed_routines()[routine]
        position += count
    raise AssertionError("opcode not covered")


class M68kTableTests(unittest.TestCase):
    def test_runs_cover_every_opcode(self):
        runs = committed_runs()
        routines = committed_routines()
        self.assertEqual(sum(count for _, count in runs), 0x10000)
        self.assertTrue(all(0 <= routine < len(routines) for routine, _ in runs))
        header = HEADER.read_text()
        self.assertIn(f"#define GENS_M68K_ROUTINE_COUNT {len(routines)}u", header)
        self.assertIn(f"#define GENS_M68K_RUN_COUNT {len(runs)}u", header)

    def test_known_opcodes(self):
        self.assertEqual(decode(0x4E71)[1], "NOP")
        self.assertEqual(decode(0x4AFC)[1], "ILLEGAL")
        self.assertEqual(decode(0x7000)[1], "MOVEQ")
        self.assertEqual(decode(0x794C)[1], "ILLEGAL")
        self.assertEqual(decode(0xF327)[1], "FLINE")
        self.assertEqual(decode(0xA000)[1], "ALINE")

    def test_parse_rejects_incomplete_tables(self):
        with self.assertRaises(ValueError):
            generator.parse("R 0 i_illegal 0 0 0 0 0 0 0 0 0\nT 0 0\n")

    def test_parse_maps_unassigned_opcodes_to_illegal(self):
        table = "R 16 i_illegal 0 0 0 0 0 0 0 0 0\nR 0 i_nop 0 0 0 0 0 0 0 0 0\n"
        table += "T 0 0\n" + "".join(f"T {n} -1\n" for n in range(1, 0x10000))
        routines, mapping = generator.parse(table)
        self.assertEqual(mapping[0], 0)
        self.assertEqual(set(mapping[1:]), {16})
        source, header = generator.generate(routines, mapping)
        self.assertIn("{0, 1}, {1, 65535},", source)
        self.assertIn("GENS_M68K_ILLEGAL", header)

    def test_committed_files_match_pinned_generator(self):
        compiler = os.environ.get("CC", "cc")
        if shutil.which(shlex.split(compiler, posix=os.name != "nt")[0]) is None:
            self.skipTest("no C compiler for the pinned generator")
        table = generator.run_generator(STAR.read_text(encoding="latin-1"), compiler)
        source, header = generator.generate(*generator.parse(table))
        self.assertEqual(source, TABLES.read_text())
        self.assertEqual(header, HEADER.read_text())


if __name__ == "__main__":
    unittest.main()
