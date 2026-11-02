from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
import shutil
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from prepare_patched_source import copy_and_patch, read_series
from extract_audio_functions import extract_patched
import prepare_patched_source


PATCH = """diff --git a/file.txt b/file.txt
--- a/file.txt
+++ b/file.txt
@@ -1 +1 @@
-before
+after
"""


class PreparePatchedSourceTests(unittest.TestCase):
    def test_patch_applies_to_copy_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "file.txt").write_bytes(b"before\n")
            (source / ".git").write_text("gitdir: missing/submodule\n", encoding="utf-8")
            patch = root / "one.patch"
            patch.write_text(PATCH, encoding="utf-8", newline="\n")
            destination = root / "output"
            copy_and_patch(source, destination, [patch])
            self.assertEqual((source / "file.txt").read_bytes(), b"before\n")
            self.assertEqual((destination / "file.txt").read_bytes(), b"after\n")
            self.assertFalse((destination / ".git").exists())
            self.assertTrue((source / ".git").exists())
            with self.assertRaises(FileExistsError):
                copy_and_patch(source, destination, [patch])
            self.assertEqual((destination / "file.txt").read_bytes(), b"after\n")

    def test_series_rejects_duplicate_and_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "one.patch").write_text(PATCH, encoding="utf-8")
            (root / "series").write_text("# comment\none.patch\n", encoding="utf-8")
            self.assertEqual(read_series(root), [root / "one.patch"])
            (root / "series").write_text("one.patch\none.patch\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                read_series(root)
            (root / "series").write_text("..\\one.patch\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "invalid"):
                read_series(root)
            (root / "series").write_text("missing.patch\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing"):
                read_series(root)

    def test_patch_inside_a_parent_git_repository(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            source = root / "source"
            source.mkdir()
            (source / "file.txt").write_bytes(b"before\n")
            patch = root / "one.patch"
            patch.write_text(PATCH, encoding="utf-8", newline="\n")
            build = root / "build"
            build.mkdir()
            destination = build / "output"
            copy_and_patch(source, destination, [patch])
            self.assertEqual((destination / "file.txt").read_bytes(), b"after\n")
            self.assertEqual((source / "file.txt").read_bytes(), b"before\n")

    def test_real_audio_patch_inside_a_parent_git_repository(self):
        core = Path(__file__).resolve().parent.parent
        original = core / "extern" / "gens-rerecording" / "Gens-rr" / "src"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            source = root / "source"
            source_files = source / "Gens-rr" / "src"
            source_files.mkdir(parents=True)
            for name in ("psg.c", "ym2612.c"):
                shutil.copyfile(original / name, source_files / name)
            build = root / "build"
            build.mkdir()
            destination = build / "output"
            copy_and_patch(source, destination, [core / "patches" / "0001-audio-cursor.patch"])
            functions = extract_patched(destination)
            self.assertIn("void PSG_Special_Update(void)", functions)
            self.assertIn("void YM2612_Special_Update(void)", functions)
            for name in ("psg.c", "ym2612.c"):
                self.assertEqual((source_files / name).read_bytes(), (original / name).read_bytes())

    def test_destination_must_be_new_and_within_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            with mock.patch.object(prepare_patched_source, "BUILD", build):
                with self.assertRaisesRegex(ValueError, "inside"):
                    prepare_patched_source.validate_destination(root / "outside")
                self.assertFalse(build.exists())
                target = prepare_patched_source.validate_destination(build / "output")
                self.assertEqual(target, (build / "output").resolve())
                target.mkdir()
                with self.assertRaisesRegex(ValueError, "already exists"):
                    prepare_patched_source.validate_destination(target)


if __name__ == "__main__":
    unittest.main()
