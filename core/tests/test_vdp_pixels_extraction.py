import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_pixels_reference", ROOT / "tools" / "extract_vdp_pixels_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_rend.asm"


class VdpPixelsExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_compositor_macros(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        self.assertIn(extractor.section(source, "%define HIGH_B", "section .data align=64"), generated)
        self.assertIn(extractor.section(source, "%macro PUTPIXEL_P0 5\n",
                                        "%macro UPDATE_PALETTE 1\n"), generated)
        for index in range(24):
            self.assertIn(f"dd reference_pixel_row.case_{index}\n", generated)
            self.assertIn(f".case_{index}:\n", generated)
        self.assertIn("PUTLINE_SPRITE_FLIP PRIO_B, 1", generated)
        self.assertIn("push dword 0\njmp [pixel_cases+eax*4]", generated)
        render = extractor.section(source, "\tDECL Render_Line\n", "\tDECL Render_Line_32X\n")
        self.assertIn(extractor.section(render, "\t.Genesis_Loop\n", "\t\tadd esp, byte 4\n"),
                      generated)
        self.assertIn("global reference_pixel_resolve", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("%macro PUTPIXEL_P0 5\n", "%macro changed 5\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_pixels_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
