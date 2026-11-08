"""Run the native callback witness in an isolated directory."""

from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_callback_rom import image


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="gens-callbacks-") as directory:
        path = Path(directory)
        (path / "rom").write_bytes(image())
        (path / "settings").write_text("{}")
        sys.exit(subprocess.run([str(binary)], cwd=path).returncode)
