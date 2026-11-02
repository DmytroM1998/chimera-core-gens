from pathlib import Path
import posixpath
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = "extern/gens-rerecording/Gens-rr/src"
SHIM = "reference/winshim"
INCLUDES = [SHIM, SHIM + "/cpuz80", SOURCE, "waterbox", "tests"]


class ReferenceHeaderTests(unittest.TestCase):
    def setUp(self):
        self.files = {}
        for directory in INCLUDES:
            for path in (ROOT / directory).iterdir():
                if path.is_file():
                    self.files[f"{directory}/{path.name}"] = path

    def resolve_include(self, parent, name):
        for directory in [posixpath.dirname(parent), *INCLUDES]:
            candidate = posixpath.normpath(posixpath.join(directory, name))
            if candidate in self.files:
                return candidate
        self.fail(f"Missing case-sensitive include {name!r} from {parent}")

    def test_cpu_headers_resolve_on_case_sensitive_filesystems(self):
        pending = [SOURCE + "/Cpu_68k.c", SOURCE + "/Cpu_Z80.c"]
        visited = set()
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            text = self.files[name].read_text(encoding="latin-1")
            for included in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.MULTILINE):
                pending.append(self.resolve_include(name, included))
        for name in ("Mem_M68k.h", "Mem_S68k.h", "Mem_SH2.h", "Misc.h", "SH2.h", "Cd_sys.h"):
            self.assertIn(SOURCE + "/" + name, visited)

    def test_aliases_forward_without_copying_declarations(self):
        aliases = {
            "mem_M68K.h": "Mem_M68k.h",
            "mem_S68K.h": "Mem_S68k.h",
            "mem_SH2.h": "Mem_SH2.h",
            "misc.h": "Misc.h",
            "sh2.h": "SH2.h",
            "cd_sys.h": "Cd_sys.h",
            "cpuz80/Mem_M68K.h": "Mem_M68k.h",
        }
        for alias, original in aliases.items():
            name = SHIM + "/" + alias
            text = self.files[name].read_text()
            match = re.fullmatch(r'#include "([^"]+)"\s*', text)
            self.assertIsNotNone(match, name)
            self.assertEqual(self.resolve_include(name, match.group(1)), SOURCE + "/" + original)

    def test_driver_searches_both_shim_directories(self):
        driver = (ROOT.parent / "chimera-build" / "driver.sh").read_text()
        self.assertIn('-I"$work/core/reference/winshim" '
                      '-I"$work/core/reference/winshim/cpuz80"', driver)


if __name__ == "__main__":
    unittest.main()
