import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_register_reference", ROOT / "tools" / "extract_vdp_register_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpRegisterExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_blocks(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("\tH_Scroll_Mask_Table:\n", "section .bss align=64"),
            ("\tDECL VDP_Reg\n", "\tDECL DMAT_Tmp\n"),
            ("\tDECL CRam_Flag\n", "section .text align=64"),
            ("\tTable_Set_Reg:\n", "\t;void Int_Ack(void);"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertNotIn("\tDECL Write_VDP_Ctrl", generated)
        self.assertNotIn("\tDECL Update_IRQ_Line", generated)
        self.assertIn("inc dword [reference_irq_calls]", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\tTable_Set_Reg:\n", "\tchanged:\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_register_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
