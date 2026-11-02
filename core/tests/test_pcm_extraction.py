import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("extract_pcm_reference", ROOT / "tools" / "extract_pcm_reference.py")
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src"


class PcmExtractionTests(unittest.TestCase):
    def test_exact_c_and_assembly_blocks(self):
        cpp = (SOURCE / "G_dsound.cpp").read_text(encoding="latin-1")
        generated = extractor.extract_cpp(cpp)
        for name in ("lowpass", "Write_Sound_Stereo", "Write_Sound_Mono",
                     "Dump_Sound_Stereo", "Dump_Sound_Mono"):
            self.assertIn(extractor.extract_function(cpp, name), generated)
        assembly = (SOURCE / "Misc.asm").read_text(encoding="latin-1")
        output = extractor.extract_mmx(assembly)
        self.assertIn(assembly[assembly.index("\t; void Write_Sound_Mono_MMX"):], output)

    def test_detects_moved_or_ambiguous_mmx_tail(self):
        assembly = (SOURCE / "Misc.asm").read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract_mmx(assembly.replace("DECL Write_Sound_Stereo_MMX", "DECL Changed"))
        with self.assertRaises(ValueError):
            extractor.extract_mmx(assembly + "\n\tDECL Another_Function\n")

    def test_cli_refuses_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference"
            command = [sys.executable, str(ROOT / "tools" / "extract_pcm_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = (output / "pcm_reference.asm").read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((output / "pcm_reference.asm").read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
