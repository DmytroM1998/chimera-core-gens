import importlib.util
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(tool, *args):
    return subprocess.run([sys.executable, str(TOOLS / tool), *map(str, args)],
                          capture_output=True, text=True)


class HeadlessToolTests(unittest.TestCase):
    def test_input_script_is_deterministic(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            self.assertEqual(run("headless_input.py", 2000, 7, a).returncode, 0)
            self.assertEqual(run("headless_input.py", 2000, 7, b).returncode, 0)
            self.assertEqual(a.read_text(), b.read_text())
            lines = a.read_text().splitlines()
            self.assertEqual(len(lines), 2000)
            self.assertTrue(all(line == "0 0" for line in lines[:120]))
            self.assertTrue(any(line != "0 0" for line in lines))

    def test_gmv_layout_follows_movie_cpp(self):
        with tempfile.TemporaryDirectory() as d:
            script, movie = Path(d) / "script", Path(d) / "movie"
            # P1 Up + X, P2 B + Mode
            script.write_text("101 820\n0 0\n")
            result = run("make_test_gmv.py", script, movie, "--p1", "6", "--p2", "3")
            self.assertEqual(result.returncode, 0, result.stderr)
            data = movie.read_bytes()
            self.assertEqual(len(data), 64 + 6)
            self.assertEqual(data[:16], b"Gens Movie TESTA")
            self.assertEqual(data[20:23], b"63\x00")
            # active low: P1 Up, P2 B, then P1 X in the low nibble and P2 Mode in the high one
            self.assertEqual(data[64:67], bytes([0xFE, 0xDF, 0x7E]))
            self.assertEqual(data[67:70], b"\xff\xff\xff")

    def test_gmv_team_player_has_no_extended_buttons(self):
        with tempfile.TemporaryDirectory() as d:
            script, movie = Path(d) / "script", Path(d) / "movie"
            script.write_text("101 2\n")
            run("make_test_gmv.py", script, movie, "--flags", "0x20")
            data = movie.read_bytes()
            self.assertEqual(data[22], 0x20)
            self.assertEqual(data[64:67], bytes([0xFE, 0xFD, 0xFF]))

    def test_trace_comparison(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            a.write_text("0 m68k 1\n0 pcm 2\n1 m68k 3\n")
            b.write_text("0 m68k 1\n0 pcm 9\n1 m68k 3\n")
            result = run("compare_headless_traces.py", a, b)
            self.assertEqual(result.returncode, 1)
            self.assertIn("frame 0 (pcm)", result.stdout)
            result = run("compare_headless_traces.py", "--skip", "pcm", a, b)
            self.assertEqual(result.returncode, 0, result.stdout)
            b.write_text("0 m68k 1\n0 pcm 2\n")
            self.assertEqual(run("compare_headless_traces.py", a, b).returncode, 1)

    def test_trace_comparison_requires_changing_blocks(self):
        with tempfile.TemporaryDirectory() as d:
            a = Path(d) / "a"
            a.write_text("0 m68k 1\n0 z80 5\n1 m68k 3\n1 z80 5\n")
            result = run("compare_headless_traces.py", "--require-changing", "m68k", a, a)
            self.assertEqual(result.returncode, 0, result.stdout)
            result = run("compare_headless_traces.py", "--require-changing", "z80", a, a)
            self.assertEqual(result.returncode, 1)
            self.assertIn("z80 block never changes", result.stdout)

    def test_elf_aliases(self):
        aliases = load("elf_aliases")

        def elf32(defined, undefined):
            names = b"\0" + b"\0".join(n.encode() for n in defined + undefined) + b"\0"
            symbols = [b"\0" * 16]
            offset = 1
            for i, name in enumerate(defined + undefined):
                shndx = 1 if i < len(defined) else 0
                symbols.append(struct.pack("<IIIBBH", offset, 0, 0, 0x10, 0, shndx))
                offset += len(name) + 1
            symtab = b"".join(symbols)
            header_size, section_size = 52, 40
            strtab_offset = header_size
            symtab_offset = strtab_offset + len(names)
            sections_offset = symtab_offset + len(symtab)
            header = bytearray(header_size)
            header[0:5] = b"\x7fELF\x01"
            struct.pack_into("<I", header, 0x20, sections_offset)
            struct.pack_into("<HH", header, 0x2E, section_size, 3)
            sections = b"\0" * section_size
            sections += struct.pack("<IIIIIIIIII", 0, 3, 0, 0, strtab_offset, len(names), 0, 0, 1, 0)
            sections += struct.pack("<IIIIIIIIII", 0, 2, 0, 0, symtab_offset, len(symtab), 1, 0, 4, 16)
            return bytes(header) + names + symtab + sections

        with tempfile.TemporaryDirectory() as d:
            asm, c, other = Path(d) / "asm.o", Path(d) / "c.o", Path(d) / "coff.obj"
            asm.write_bytes(elf32(["_main68k_exec"], ["_hook_pc", "M68K_RB"]))
            c.write_bytes(elf32(["hook_pc", "M68K_RB"], ["main68k_exec"]))
            other.write_bytes(b"\x4c\x01not elf")
            result = run("elf_aliases.py", asm, c, other)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(),
                             ["_hook_pc = hook_pc;", "main68k_exec = _main68k_exec;"])
            self.assertEqual(aliases.symbols(str(other)), (set(), set()))

    def test_manifest(self):
        fetch = load("fetch_test_roms")
        entries = fetch.read_manifest(ROOT / "tests" / "headless-roms.txt")
        names = [name for _, name, _ in entries]
        self.assertEqual(len(names), len(set(names)))
        self.assertIn("240p.bin", names)
        self.assertTrue(all(len(digest) == 64 for digest, _, _ in entries))
        for name in ("240p", "sgdk_game_platformer", "sgdk_snd_xgm-player", "sgdk_joy-test",
                     "battle-4tris"):
            self.assertIn(name + ".bin", names)

    def test_download_retries_http_500(self):
        fetch = load("fetch_test_roms")
        url = "https://example.test/rom.bin"
        error = urllib.error.HTTPError(url, 500, "Internal Server Error", {}, None)
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / "rom.bin"
            with mock.patch.object(fetch.urllib.request, "urlopen",
                                   side_effect=[error, io.BytesIO(b"rom")]) as request, \
                    mock.patch.object(fetch.time, "sleep") as sleep:
                fetch.download(url, target)
            self.assertEqual(request.call_count, 2)
            sleep.assert_called_once_with(2)
            self.assertEqual(target.read_bytes(), b"rom")
            self.assertFalse(target.with_name("rom.bin.part").exists())

    def test_download_retries_are_bounded(self):
        fetch = load("fetch_test_roms")
        url = "https://example.test/rom.bin"
        error = urllib.error.HTTPError(url, 503, "Unavailable", {}, None)
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / "rom.bin"
            with mock.patch.object(fetch.urllib.request, "urlopen", side_effect=error) as request, \
                    mock.patch.object(fetch.time, "sleep") as sleep:
                with self.assertRaises(urllib.error.URLError) as raised:
                    fetch.download(url, target)
            self.assertIn(url, str(raised.exception))
            self.assertEqual(request.call_count, 4)
            self.assertEqual(sleep.call_args_list, [mock.call(2), mock.call(4), mock.call(8)])
            self.assertFalse(target.exists())

    def test_download_does_not_retry_http_404(self):
        fetch = load("fetch_test_roms")
        url = "https://example.test/rom.bin"
        error = urllib.error.HTTPError(url, 404, "Not Found", {}, None)
        with tempfile.TemporaryDirectory() as d:
            with mock.patch.object(fetch.urllib.request, "urlopen", side_effect=error) as request, \
                    mock.patch.object(fetch.time, "sleep") as sleep:
                with self.assertRaises(urllib.error.URLError):
                    fetch.download(url, Path(d) / "rom.bin")
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()

    def test_download_timeout_keeps_existing_target(self):
        fetch = load("fetch_test_roms")
        url = "https://example.test/rom.bin"
        with tempfile.TemporaryDirectory() as d:
            target = Path(d) / "rom.bin"
            target.write_bytes(b"existing")
            with mock.patch.object(fetch.urllib.request, "urlopen",
                                   side_effect=lambda *args, **kwargs: io.BytesIO(b"rom")), \
                    mock.patch.object(fetch.shutil, "copyfileobj", side_effect=TimeoutError), \
                    mock.patch.object(fetch.time, "sleep"):
                with self.assertRaises(urllib.error.URLError):
                    fetch.download(url, target)
            self.assertEqual(target.read_bytes(), b"existing")
            self.assertFalse(target.with_name("rom.bin.part").exists())


if __name__ == "__main__":
    unittest.main()
