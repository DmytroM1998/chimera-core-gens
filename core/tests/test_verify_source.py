import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from verify_source import blob_id, verify


def git_blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


class VerifySourceTests(unittest.TestCase):
    def test_complete_and_changed_trees(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "nested").mkdir()
            file = root / "nested" / "file"
            file.write_bytes(b"hello\n")
            expected = {"nested/file": git_blob(b"hello\n")}
            self.assertEqual(blob_id(file), expected["nested/file"])
            self.assertEqual(verify(root, expected), [])
            file.write_bytes(b"changed")
            (root / "extra").write_bytes(b"extra")
            self.assertEqual(verify(root, expected), [
                "Changed file: nested/file", "Unexpected file: extra"
            ])
            file.unlink()
            self.assertIn("Missing file: nested/file", verify(root, expected))

    def test_missing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "missing"
            self.assertEqual(len(verify(source, {"file": git_blob(b"")})), 1)


if __name__ == "__main__":
    unittest.main()
