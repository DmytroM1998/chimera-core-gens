import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "Mem_M68k.asm"
SPEC = importlib.util.spec_from_file_location(
    "extract_genesis_bus_reference", ROOT / "tools" / "extract_genesis_bus_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)


class ExtractionTest(unittest.TestCase):
    def test_extracts_original_blocks_without_changing_instructions(self):
        source = SOURCE.read_text(encoding="ascii")
        generated = extractor.extract(source)
        for start, end in (
            ("\tDECL Genesis_M68K_Read_Byte_Table", "\t; Sega CD Default Jump Table"),
            ("\tDECL M68K_Read_Byte_Table", "section .bss align=64"),
            ("\tDECL Ram_68k", "section .text align=64"),
            ("\t.Genesis\n", "\t._32X\n"),
            ("\t;unsigned char M68K_RB", "\t;void M68K_WBC"),
            ("\t;void M68K_WB(unsigned int Adr, unsigned char Data)",
             '%include "Mem_M68K_CD.inc"'),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertNotIn('%include "Mem_M68K_CD.inc"', generated)
        self.assertNotIn("\tDECL M68K_WBC", generated)

    def test_rejects_missing_or_ambiguous_markers(self):
        source = SOURCE.read_text(encoding="ascii")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\t.Genesis\n", "\t.changed\n"))
        with self.assertRaises(ValueError):
            extractor.extract(source + "\n\t.Genesis\n")

    def test_cli_writes_new_file_and_refuses_overwrite(self):
        tool = ROOT / "tools" / "extract_genesis_bus_reference.py"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(tool), str(SOURCE), str(output)]
            first = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(first.returncode, 0, first.stderr.decode())
            expected = extractor.extract(SOURCE.read_text(encoding="ascii"))
            self.assertEqual(output.read_text(encoding="ascii"), expected)
            second = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_text(encoding="ascii"), expected)


if __name__ == "__main__":
    unittest.main()
