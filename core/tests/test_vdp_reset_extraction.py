import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_reset_reference", ROOT / "tools" / "extract_vdp_reset_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpResetExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_reset_and_setters(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("\tH_Scroll_Mask_Table:\n", "section .bss align=64"),
            ("\tDECL VDP_Reg\n", "section .text align=64"),
            ("\t;void Reset_VDP(void)", "\terror:\n"),
            ("\tTable_Set_Reg:\n", "\t;void Int_Ack(void);"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertIn(source[source.index("\t;void Int_Ack(void);"):], generated)
        self.assertIn("push ecx\npush edx\n", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\t;void Reset_VDP(void)", "\t;changed"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_reset_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
