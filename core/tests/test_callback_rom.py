import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "make_callback_rom.py"
SPEC = importlib.util.spec_from_file_location("make_callback_rom", TOOL)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class CallbackRomTests(unittest.TestCase):
    def test_vectors_header_and_watched_instructions(self):
        rom = generator.image()
        self.assertEqual(len(rom), 0x400)
        self.assertEqual(struct.unpack_from(">II", rom), (0xFFFF00, 0x200))
        self.assertEqual(struct.unpack_from(">I", rom, 0x80)[0], 0x280)
        self.assertEqual(rom[0x100:0x10C], b"SEGA GENESIS")
        for pc, opcode in ((0x200, 0x7001), (0x208, 0x10BC), (0x212, 0x217C),
                           (0x224, 0x227C), (0x22A, 0x2303), (0x22E, 0x4E71),
                           (0x230, 0x1839), (0x236, 0x4E40), (0x280, 0x4E73)):
            self.assertEqual(struct.unpack_from(">H", rom, pc)[0], opcode)
        self.assertEqual(0x248 + struct.unpack_from(">h", rom, 0x248)[0], 0x200)

    def test_cli_writes_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "callbacks.bin"
            command = [sys.executable, str(TOOL), str(path)]
            result = subprocess.run(command, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.read_bytes(), generator.image())
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(path.read_bytes(), generator.image())


if __name__ == "__main__":
    unittest.main()
