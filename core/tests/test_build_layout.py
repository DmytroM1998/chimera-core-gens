from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BuildLayoutTests(unittest.TestCase):
    def test_minibox_nested_make_is_bounded(self):
        import subprocess
        import sys
        import tempfile

        driver = (ROOT / "chimera-build" / "driver.sh").read_text()
        script = driver.split("<<'PYJOBS'\n", 1)[1].split("\nPYJOBS", 1)[0]
        with tempfile.TemporaryDirectory() as directory:
            recipe = Path(directory) / "build.ninja"
            recipe.write_text("command = /usr/bin/make -j install\n")
            for _ in range(2):
                result = subprocess.run([sys.executable, "-c", script, str(recipe)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(recipe.read_text(), "command = /usr/bin/make -j1 install\n")
            recipe.write_text("command = unexpected\n")
            result = subprocess.run([sys.executable, "-c", script, str(recipe)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Cannot safely limit", result.stderr)
            self.assertEqual(recipe.read_text(), "command = unexpected\n")
        self.assertIn('meson compile -C "$build" -j 1', driver)

    def test_source_mounts(self):
        compose = (ROOT / "compose.yaml").read_text()
        self.assertIn("../chimera-common-minibox:/sources/minibox:ro", compose)
        self.assertIn(".:/sources/repository:ro", compose)
        self.assertIn("./core:/sources/core:ro", compose)
        self.assertIn("./build/artifacts:/out", compose)
        self.assertNotIn("/sources/chimera", compose)
        self.assertNotIn("/sources/gens:", compose)

    def test_submodule_and_driver_paths(self):
        self.assertIn("path = core/extern/gens-rerecording",
                      (ROOT / ".gitmodules").read_text())
        self.assertFalse((ROOT / "core" / ".gitmodules").exists())
        driver = (ROOT / "chimera-build" / "driver.sh").read_text()
        self.assertIn("gens=/sources/repository/core/extern/gens-rerecording", driver)
        self.assertIn('export GENS_REFERENCE="$gens"', driver)
        self.assertNotIn("/sources/gens", driver)
        self.assertNotIn("build_chimera", driver)
        self.assertNotIn("/sources/chimera", driver)
        self.assertIn("core) check_prepared; build_minibox; build_core ;;", driver)
        self.assertIn('git -C "$work/minibox" submodule update --init --recursive', driver)

    def test_docker_build_context(self):
        dockerfile = (ROOT / "Dockerfile").read_text()
        self.assertIn("COPY chimera-build/driver.sh", dockerfile)
        self.assertTrue((ROOT / "chimera-build" / "driver.sh").is_file())
        self.assertIn("!chimera-build/driver.sh", (ROOT / ".dockerignore").read_text())

    def test_only_one_service_builds_the_shared_image(self):
        compose = (ROOT / "compose.yaml").read_text()
        self.assertEqual(compose.count("build:\n"), 1)
        builder = compose.split("services:", 1)[0]
        self.assertNotIn("build:\n", builder)
        prepare = compose.split("  prepare:\n", 1)[1].split("  compile:\n", 1)[0]
        self.assertIn("    build:\n", prepare)

    def test_machine_progress_labels_each_runner_and_comparison(self):
        driver = (ROOT / "chimera-build" / "driver.sh").read_text()
        machine = driver.split("check_machine() {", 1)[1].split("\ncheck_vdp_machine()", 1)[0]
        ordered = [
            "Whole-machine comparison: building reference and port",
            "Synthetic-input tests: original Gens vs port (6000 frames each)",
            "Testing ROM: $name.bin",
            "Running original Gens reference...",
            'timeout 900s "$directory/reference" "$rom" 6000',
            "Running port...",
            'timeout 900s "$directory/port" "$rom" 6000',
            "PASS: $name.bin ($count programs matched)",
            "Negative control: 240p.bin (300 frames)",
            "PASS: intentional difference detected",
            "Generated GMV movie tests: original Gens vs Chimera core (5000 frames each)",
            "Testing ROM: $1.bin (generated GMV, P1=$2-button, P2=$3-button, flags=$4)",
            "Running original Gens movie playback...",
            'timeout 900s "$directory/reference" "$run/game.bin" 5000',
            "Running Chimera core movie playback...",
            'timeout 900s "$directory/run-native" "$run" play 5000',
            "PASS: $1.bin movie traces match",
        ]
        positions = [machine.index(text) for text in ordered]
        self.assertEqual(positions, sorted(positions))


if __name__ == "__main__":
    unittest.main()
