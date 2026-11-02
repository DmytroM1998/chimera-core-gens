import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_irq_reference", ROOT / "tools" / "extract_vdp_irq_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpIrqExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_irq_routines(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        self.assertIn(source[source.index("\t;void Int_Ack(void);"):], generated)
        self.assertIn(extractor.section(source, "\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n"),
                      generated)
        self.assertIn("mov eax,[esp+4]\ncall Int_Ack\nret", generated)

    def test_rejects_missing_or_duplicate_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\t;void Int_Ack(void);", "\t;changed"))
        with self.assertRaises(ValueError):
            extractor.extract(source + "\t;void Int_Ack(void);")

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_irq_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
