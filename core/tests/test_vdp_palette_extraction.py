import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_palette_reference", ROOT / "tools" / "extract_vdp_palette_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_rend.asm"


class VdpPaletteExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_palette_macros(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        self.assertIn(extractor.section(source, "%macro UPDATE_PALETTE 1\n",
                                        "%macro RENDER_LINE_SCROLL_B 3\n"), generated)
        self.assertIn("test byte [CRam_Flag],1", generated)
        self.assertIn("UPDATE_PALETTE32 0", generated)
        self.assertIn("UPDATE_PALETTE32 1", generated)
        self.assertNotIn("%macro RENDER_LINE_SCROLL_B 3", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("%macro UPDATE_PALETTE 1\n", "%macro changed 1\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_palette_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
