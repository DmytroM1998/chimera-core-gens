import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "extract_vdp_memory_dma_reference", ROOT / "tools" / "extract_vdp_memory_dma_reference.py"
)
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)
SOURCE = ROOT / "extern" / "gens-rerecording" / "Gens-rr" / "src" / "vdp_io.asm"


class VdpMemoryDmaExtractionTests(unittest.TestCase):
    def test_extracts_unchanged_genesis_blocks(self):
        source = SOURCE.read_text(encoding="latin-1")
        generated = extractor.extract(source)
        for start, end in (
            ("\tCD_Table:\n", "\tDECL Genesis_Started\n"),
            ("\tDECL VDP_Reg\n", "\tDECL ScrA_Addr\n"),
            ("\tDECL Ctrl\n", "section .text align=64"),
            ("%macro DMA_LOOP 2\n", ";***********************************************"),
            ("\t;void Update_DMA(void)", "\t;unsigned short Read_VDP_Data(void)"),
            ("\t;void Write_VDP_Ctrl(unsigned short Data)", "\t.RAMPRG_To_VRam_Star\n"),
            ("\t.End_DMA\n", "\tV_RAM_Copy:\n"),
            ("\tNO_DMA:\n", "\tTable_Set_Reg:\n"),
        ):
            self.assertIn(extractor.section(source, start, end), generated)
        self.assertIn(".RAMPRG_To_VRam_Star:\nud2", generated)
        self.assertIn("sub [reference_cycle_counter],eax", generated)

    def test_rejects_missing_marker(self):
        source = SOURCE.read_text(encoding="latin-1")
        with self.assertRaises(ValueError):
            extractor.extract(source.replace("\t.End_DMA\n", "\t.changed\n"))

    def test_cli_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "reference.asm"
            command = [sys.executable, str(ROOT / "tools" / "extract_vdp_memory_dma_reference.py"),
                       str(SOURCE), str(output)]
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            expected = output.read_bytes()
            result = subprocess.run(command, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
