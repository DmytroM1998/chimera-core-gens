import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_read_reference", ROOT / "tools" / "extract_vdp_read_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpReadExtractionTests(unittest.TestCase):
    def test_extracts_original_blocks(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n"),
            ("\tDECL Ctrl\n", "\tDECL DMAT_Tmp\n"),
            ("\tDECL VDP_Status\n", "\tDECL CRam_Flag\n"),
            ("\terror:\n", "\t;void Update_DMA(void)"),
            ("\t.Loop_HC\n", "\t\tmov dword [VSRam_Over + 28], 0"),
            ("\t;unsigned short Read_VDP_Data(void)",
             "\t;void Write_Byte_VDP_Data(unsigned char Data)"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertNotIn("\tDECL Write_Byte_VDP_Data", generated)

    def test_rejects_changed_markers(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\t.Loop_HC\n", "\t.changed\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_read_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
