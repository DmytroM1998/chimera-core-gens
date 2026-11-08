from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


DRIVER = Path(__file__).resolve().parents[2] / "chimera-build" / "driver.sh"
SCRIPT = DRIVER.read_text().split("<<'PYJOBS'\n", 1)[1].split("\nPYJOBS", 1)[0]


class DriverParallelismTests(unittest.TestCase):
    def apply_guard(self, recipe):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "build.ninja"
            path.write_text(recipe)
            result = subprocess.run(
                [sys.executable, "-c", SCRIPT, str(path)],
                capture_output=True, text=True,
            )
            return result, path.read_text()

    def test_make_and_gmake_are_bounded_idempotently(self):
        for make in ("make", "gmake"):
            with self.subTest(make=make):
                recipe = f" COMMAND = /usr/bin/{make} -j install\n"
                result, bounded = self.apply_guard(recipe)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(bounded, recipe.replace("-j install", "-j1 install"))
                result, repeated = self.apply_guard(bounded)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(repeated, bounded)

    def test_unexpected_or_ambiguous_commands_are_not_modified(self):
        for recipe in (
            " COMMAND = /usr/bin/make -j8 install\n",
            " COMMAND = /usr/bin/gmake install\n",
            " COMMAND = /opt/make -j install\n",
            " COMMAND = /opt/usr/bin/make -j install\n",
            " COMMAND = /usr/bin/make -j install-strip\n",
            " COMMAND = /usr/bin/make -j install\n COMMAND = /usr/bin/gmake -j install\n",
            " COMMAND = /usr/bin/make -j install\n COMMAND = /usr/bin/make -j install\n",
        ):
            with self.subTest(recipe=recipe):
                result, unchanged = self.apply_guard(recipe)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Cannot safely limit its parallelism", result.stderr)
                self.assertEqual(unchanged, recipe)


if __name__ == "__main__":
    unittest.main()
