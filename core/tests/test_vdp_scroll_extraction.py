import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_scroll_reference", ROOT / "tools" / "extract_vdp_scroll_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_rend.asm"


class VdpScrollExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_renderer_macros(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("%define HIGH_B", "section .data align=64"),
            ("%macro GET_X_OFFSET 1\n", "%macro MAKE_SPRITE_STRUCT 1\n"),
            ("%macro PUTPIXEL_P0 5\n", "%macro UPDATE_PALETTE 1\n"),
            ("%macro RENDER_LINE_SCROLL_B 3\n", "%macro RENDER_LINE_SPR 2\n"),
            ("\tMask_N", "\nsection .bss align=64\n"),
            ("\tDECL Data_Spr\n", "\tDECL _32X_Rend_Mode"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        for mode in ("0,0,0", "1,0,0", "0,0,1", "1,0,1", "0,1,0", "1,1,0", "0,1,1", "1,1,1"):
            self.assertIn("RENDER_LINE_SCROLL_B " + mode, generated)
            self.assertIn("RENDER_LINE_SCROLL_A_WIN " + mode, generated)
        self.assertIn("push dword [test_row_offset]", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("%macro RENDER_LINE_SCROLL_B 3\n", "%macro changed 3\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_scroll_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
