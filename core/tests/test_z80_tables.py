import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "generate_z80_tables", ROOT / "tools" / "generate_z80_tables.py"
)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "z80.asm"


class Z80TableTests(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding="latin-1")

    def test_committed_files_match_pinned_source(self):
        self.assertEqual(generator.generate(self.source),
                         (ROOT / "waterbox" / "gens_z80_tables.c").read_text())
        self.assertEqual(generator.header(), (ROOT / "waterbox" / "gens_z80_tables.h").read_text())

    def test_every_table_has_256_entries(self):
        tables = generator.parse_tables(self.source)
        self.assertEqual(set(tables), set(generator.TABLES))
        for entries in tables.values():
            self.assertEqual(len(entries), 256)

    def test_known_quirks_are_preserved(self):
        tables = generator.parse_tables(self.source)
        self.assertEqual(tables["DD_Table"][0x1C], "Z80I_DEC_E")
        self.assertEqual(tables["DD_Table"][0xFC], "Z80I_CALLNS_NN")
        self.assertEqual(tables["ED_Table"][0x00], "Z80I_NOP")

    def test_classification(self):
        self.assertEqual(generator.classify("Z80I_LD_B_hIX"), ("LD_R_R", 0, 7))
        self.assertEqual(generator.classify("Z80I_LD_H_mIYd"), ("LD_R_MXYD", 4, 1))
        self.assertEqual(generator.classify("Z80I_LD_mIXd_N"), ("LD_MXYD_N", 0, 0))
        self.assertEqual(generator.classify("Z80I_LD_mIXd_N")[2], 0)
        self.assertEqual(generator.classify("Z80I_LD_A_R"), ("LD_A_R", 0, 0))
        self.assertEqual(generator.classify("Z80I_ADD_B"), ("ALU_R", 0, 0))
        self.assertEqual(generator.classify("Z80I_CP_hIY"), ("ALU_R", 7, 9))
        self.assertEqual(generator.classify("Z80I_DEC_mIYd"), ("INCDEC_MXYD", 1, True))
        self.assertEqual(generator.classify("Z80I_JP_NN")[0], "JP")
        for entries in generator.parse_tables(self.source).values():
            for name in entries:
                self.assertNotEqual(generator.classify(name)[0], "UNIMPLEMENTED", name)
        self.assertEqual(generator.classify("PREFIXE_DDCB")[0], "PREFIX_DDCB")
        with self.assertRaises(ValueError):
            generator.classify("bogus")

    def test_missing_table_is_rejected(self):
        with self.assertRaises(ValueError):
            generator.parse_tables(self.source.replace("\tFDCB_Table\n", "\tOTHER_Table\n"))


if __name__ == "__main__":
    unittest.main()
