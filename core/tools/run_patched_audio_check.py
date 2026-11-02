#!/usr/bin/env python3
"""Build and run the isolated patched-audio test on Linux."""

import argparse
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile

from extract_audio_functions import extract_patched
from prepare_patched_source import BUILD, PATCHES, SOURCE, copy_and_patch, read_series
from verify_source import git_tree, verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True,
                        help="Git repository containing the pinned Gens revision")
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() != "x86_64":
        print("Patched audio build checks require Linux x86-64", file=sys.stderr)
        return 1
    try:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        patches = read_series(PATCHES)
        problems = verify(SOURCE, git_tree(args.reference))
        if problems:
            print("\n".join(problems), file=sys.stderr)
            return 1
        if BUILD.is_symlink():
            raise ValueError(f"build directory is a symlink: {BUILD}")
        BUILD.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="audio-check-", dir=BUILD) as directory:
            temporary = Path(directory)
            source = temporary / "source"
            copy_and_patch(SOURCE, source, patches)
            (temporary / "audio_special.inc").write_text(
                extract_patched(source), encoding="utf-8", newline="\n")
            executable = temporary / "test-gens-audio-patch"
            root = BUILD.parent
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                "-I" + str(root / "waterbox"), "-I" + str(temporary),
                str(root / "waterbox" / "gens_audio.c"),
                str(root / "tests" / "test_gens_audio_patch.c"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)
            for argument, label in (("p", "PSG"), ("y", "YM")):
                result = subprocess.run([str(executable), argument], capture_output=True, text=True)
                expected = f"gens: invalid {label} audio cursor (2)"
                if result.returncode != -signal.SIGABRT or expected not in result.stderr:
                    raise ValueError(f"{label} invalid-timing control did not fail as expected")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Patched audio check failed: {error}", file=sys.stderr)
        return 1
    print("Patched PSG/YM audio cursor checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
