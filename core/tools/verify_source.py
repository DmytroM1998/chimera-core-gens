#!/usr/bin/env python3
"""Verify the checked-out Gens submodule against the selected upstream Git revision."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys

REVISION = "b948995d15034983cbf5562610586d93e0b25a9b"
SOURCE = Path(__file__).resolve().parent.parent / "extern" / "gens-rerecording"


def git_tree(reference):
    result = subprocess.run(
        ["git", "-C", str(reference), "ls-tree", "-r", "-z", "--full-tree", REVISION],
        check=True,
        stdout=subprocess.PIPE,
    )
    expected = {}
    for record in result.stdout.split(b"\0"):
        if not record:
            continue
        info, raw_name = record.split(b"\t", 1)
        mode, kind, oid = info.split()
        if kind != b"blob" or mode not in (b"100644", b"100755"):
            raise ValueError(f"Unsupported Git tree entry: {os.fsdecode(raw_name)}")
        name = os.fsdecode(raw_name)
        path = PurePosixPath(name)
        if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
            raise ValueError(f"Unsafe Git path: {name}")
        if name in expected:
            raise ValueError(f"Duplicate Git path: {name}")
        expected[name] = oid.decode("ascii")
    if not expected:
        raise ValueError("Selected Git tree is empty")
    return expected


def blob_id(path):
    size = path.stat().st_size
    digest = hashlib.sha1(b"blob " + str(size).encode("ascii") + b"\0")
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(source, expected):
    problems = []
    actual = set()
    if not source.is_dir() or source.is_symlink():
        return [f"Source directory is missing or is a symlink: {source}"]
    def raise_walk_error(error):
        raise error

    for root, dirs, files in os.walk(source, followlinks=False, onerror=raise_walk_error):
        root = Path(root)
        dirs[:] = [name for name in dirs if name != ".git"]
        files = [name for name in files if name != ".git"]
        for name in dirs[:]:
            path = root / name
            if path.is_symlink():
                problems.append(f"Unexpected symlink: {path.relative_to(source)}")
                dirs.remove(name)
        for name in files:
            path = root / name
            relative = path.relative_to(source).as_posix()
            actual.add(relative)
            if path.is_symlink() or not path.is_file():
                problems.append(f"Not a regular file: {relative}")
            elif relative not in expected:
                problems.append(f"Unexpected file: {relative}")
            elif blob_id(path) != expected[relative]:
                problems.append(f"Changed file: {relative}")
    problems.extend(f"Missing file: {name}" for name in expected.keys() - actual)
    return sorted(problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True,
                        help="Git repository containing the pinned Gens revision")
    args = parser.parse_args()
    if not SOURCE.is_dir() or not any(SOURCE.iterdir()):
        print("extern/gens-rerecording is empty. Run: git submodule update --init extern/gens-rerecording",
              file=sys.stderr)
        return 2
    try:
        expected = git_tree(args.reference)
        problems = verify(SOURCE, expected)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Source verification failed: {error}", file=sys.stderr)
        return 2
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"Verified {len(expected)} files against Git revision {REVISION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
