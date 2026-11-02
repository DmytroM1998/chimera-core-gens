import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_render_reference", ROOT / "tools" / "extract_vdp_render_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_rend.asm"


class VdpRenderExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_render_line(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("%macro GET_X_OFFSET 1\n", "\tDECL Render_Line_32X\n"),
            ("\tDECL TAB336\n", "\tDECL TAB320\n"),
            ("\tMask_N", "\nsection .bss align=64\n"),
            ("\tDECL Data_Spr\n", "\tDECL _32X_Rend_Mode"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertIn("DECL Render_Line\n", generated)

    def test_declares_every_global_used_by_the_routine(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for name in extractor.WORDS + extractor.BYTE_FLAGS:
            self.assertIn(f"DECL {name}\n", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\tDECL Render_Line_32X\n", "\tDECL changed\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_render_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
