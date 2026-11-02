import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_video_reference", ROOT / "tools" / "extract_vdp_video_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "Gens.cpp"
HEADER = SOURCE.with_name("vdp_io.h")


class VdpVideoExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_function_and_register_layout(self):
        source, header = SOURCE.read_text(encoding="latin-1"), HEADER.read_text(encoding="latin-1")
        generated = extractor.extract(source, header)
        self.assertIn(extractor.extract_function(source, "Render_MD_Screen"), generated)
        self.assertIn(extractor.section(header, "struct Reg_VDP_Type {", "\n// XXX:"), generated)
        self.assertIn("TAB336[240]", generated)
        self.assertIn("FakeVDPScreen=false;\nRender_MD_Screen();", generated)
        self.assertNotIn("void Render_MD_Screen32X", generated)

    def test_rejects_missing_function(self):
        source, header = SOURCE.read_text(encoding="latin-1"), HEADER.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("void Render_MD_Screen()", "void changed()"), header)

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.cpp"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_video_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
