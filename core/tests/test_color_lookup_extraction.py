import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_color_lookup_reference", ROOT / "tools" / "extract_color_lookup_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "Gens.cpp"


class ColorLookupExtractionTests(unittest.TestCase):
    def test_extracts_function_with_only_cast_normalization(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        function = extractor.extract_function(source, "Recalculate_Palettes")
        self.assertIn(function.replace("unsigned int (", "(unsigned int)("), generated)
        self.assertEqual(function.count("unsigned int ("), 12)
        self.assertIn('extern "C" void reference_color_lookup', generated)
        self.assertIn("_32X_Palette_16B[65536]", generated)
        self.assertNotIn("void Check_Country_Order", generated)

    def test_rejects_missing_or_duplicate_function(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("void Recalculate_Palettes(void)", "void changed(void)"))
        with self.assertRaises(ValueError):
            extractor.extract(source + "\nvoid Recalculate_Palettes(void) {}\n")

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.cpp"
            command = [sys.executable, str(ROOT / "tools" / "extract_color_lookup_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
