#!/usr/bin/env python3
"""Copy the pinned Gens source into build/ and apply the ordered patches."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from verify_source import SOURCE, git_tree, verify

ROOT = Path(__file__).resolve().parent.parent
PATCHES = ROOT / "patches"
BUILD = ROOT / "build"


def read_series(directory):
    names = []
    for line in (directory / "series").read_text(encoding="utf-8").splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*\.patch", name):
            raise ValueError(f"invalid patch name: {name}")
        if name in names:
            raise ValueError(f"duplicate patch: {name}")
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"missing or unsafe patch: {path}")
        names.append(name)
    if not names:
        raise ValueError("patch series is empty")
    return [directory / name for name in names]


def validate_destination(destination):
    if BUILD.is_symlink():
        raise ValueError(f"build directory is a symlink: {BUILD}")
    build = BUILD.resolve(strict=False)
    requested = Path(destination).absolute()
    if os.path.lexists(requested):
        raise ValueError(f"destination already exists: {requested}")
    target = requested.resolve(strict=False)
    if build not in target.parents:
        raise ValueError("destination must be a new directory inside the core's build directory")
    BUILD.mkdir(exist_ok=True)
    return target


def copy_and_patch(source, destination, patches):
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git"))
    environment = os.environ.copy()
    for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
                "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES",
                "GIT_PREFIX", "GIT_NAMESPACE"):
        environment.pop(key, None)
    environment["GIT_CEILING_DIRECTORIES"] = str(Path(destination).resolve().parent)
    for patch in patches:
        command = ["git", "-c", "core.autocrlf=false", "apply"]
        patch_path = str(Path(patch).resolve())
        subprocess.run(command + ["--check", "--", patch_path], cwd=destination,
                       env=environment, check=True)
        subprocess.run(command + ["--", patch_path], cwd=destination,
                       env=environment, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True,
                        help="Git repository containing the pinned Gens revision")
    parser.add_argument("--destination", type=Path, default=BUILD / "gens-source",
                        help="new directory inside this core's build directory")
    args = parser.parse_args()
    destination = None
    try:
        patches = read_series(PATCHES)
        problems = verify(SOURCE, git_tree(args.reference))
        if problems:
            print("\n".join(problems), file=sys.stderr)
            return 1
        destination = validate_destination(args.destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        copy_and_patch(SOURCE, destination, patches)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Source preparation failed: {error}", file=sys.stderr)
        if destination is not None and os.path.lexists(destination):
            print(f"Inspect the incomplete destination before removing it: {destination}",
                  file=sys.stderr)
        return 1
    print(f"Prepared patched Gens source in {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
