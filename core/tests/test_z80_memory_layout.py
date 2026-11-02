import re
import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] / "extern" / "gens-rerecording" /
          "Gens-rr" / "src" / "Mem_Z80.asm")


def bank_follows_ram(source):
    data = source.split("section .bss align=64", 1)[1].split("section .text align=64", 1)[0]
    data = "\n".join(line.split(";", 1)[0] for line in data.splitlines())
    return re.fullmatch(
        r"\s*DECL\s+Ram_Z80\s+resb\s+\(8\s*\*\s*1024\)\s+"
        r"DECL\s+Bank_Z80\s+resd\s+1\s*", data
    ) is not None


class Z80MemoryLayoutTests(unittest.TestCase):
    def test_pinned_bank_is_immediately_after_ram(self):
        self.assertTrue(bank_follows_ram(SOURCE.read_text(encoding="ascii")))

    def test_detects_padding_or_changed_ram_size(self):
        text = SOURCE.read_text(encoding="ascii")
        for old, new in (("DECL Bank_Z80", "resb 1\n\tDECL Bank_Z80"),
                         ("resb (8 * 1024)", "resb (16 * 1024)")):
            with self.subTest(mutation=new):
                self.assertIn(old, text)
                self.assertFalse(bank_follows_ram(text.replace(old, new)))


if __name__ == "__main__":
    unittest.main()
