import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_dma_reference", ROOT / "tools" / "extract_vdp_dma_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpDmaExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_timing(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        self.assertIn(extractor.section(source, "\tDMA_Timing_Table:\n",
                                        "\tDECL Genesis_Started\n"), generated)
        self.assertIn(extractor.section(source, "\t;void Update_DMA(void)",
                                        "\t;unsigned short Read_VDP_Data(void)"), generated)
        self.assertNotIn("\tDECL Read_VDP_Data", generated)

    def test_candidate_table_matches_active_assembly_entries(self):
        source = SOURCE.read_text(encoding="latin-1")
        table = extractor.section(source, "\tDMA_Timing_Table:\n",
                                  "\tDECL Genesis_Started\n")
        expected = []
        for line in table.splitlines():
            match = re.match(r"\s*dd\s+(.*)", line.split(";", 1)[0])
            if match:
                expected.extend(int(item.strip(), 0) for item in
                                match[1].split(",") if item.strip())
        candidate = (ROOT / "waterbox" / "gens_vdp_dma.c").read_text()
        body = candidate.split("timing[16] = {", 1)[1].split("}", 1)[0]
        actual = [int(item.strip(), 0) for item in body.split(",") if item.strip()]
        self.assertEqual(len(expected), 16)
        self.assertEqual(actual, expected)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\tDMA_Timing_Table:\n", "\tchanged:\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_dma_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
