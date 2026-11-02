#!/usr/bin/env python3
"""Download the pinned test programs of tests/headless-roms.txt and check their SHA-256 sums.

Files inside the SGDK release archive are extracted with the 7z tool. Nothing is kept when a
sum differs. The destination must not exist yet.
"""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, target):
    target = Path(target)
    partial = target.with_name(target.name + ".part")
    for attempt in range(1, 5):
        print(f"Downloading {url} (attempt {attempt}/4)", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=600) as response, open(partial, "wb") as out:
                shutil.copyfileobj(response, out)
            partial.replace(target)
            return
        except (urllib.error.URLError, TimeoutError) as error:
            partial.unlink(missing_ok=True)
            retryable = not isinstance(error, urllib.error.HTTPError) or error.code in (
                429, 500, 502, 503, 504)
            if not retryable or attempt == 4:
                raise urllib.error.URLError(
                    f"{url}: download failed after {attempt} attempt(s): {error}") from error
            delay = 2 ** attempt
            print(f"{url}: {error}. Retrying in {delay} seconds.", file=sys.stderr, flush=True)
            time.sleep(delay)


def read_manifest(path):
    entries = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        digest, name, source = line.split(None, 2)
        if len(digest) != 64 or "/" in name or "\\" in name:
            raise ValueError(f"bad manifest line: {line}")
        entries.append((digest, name, source))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.destination.exists():
        parser.exit(1, f"{args.destination} already exists\n")
    entries = read_manifest(args.manifest)
    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        staged = scratch / "roms"
        staged.mkdir()
        archives = {}
        for digest, name, source in entries:
            target = staged / name
            if source.startswith("sgdk:"):
                spec = source[len("sgdk:"):]
                url_and_sum, member = spec.rsplit(":", 1)
                url, archive_sum = url_and_sum.split("#", 1)
                if url not in archives:
                    archive = scratch / f"archive{len(archives)}.7z"
                    download(url, archive)
                    if sha256(archive) != archive_sum:
                        parser.exit(1, f"{url}: SHA-256 differs from the manifest\n")
                    extracted = scratch / f"archive{len(archives)}"
                    subprocess.run(["7z", "x", "-y", f"-o{extracted}", str(archive)],
                                   check=True, stdout=subprocess.DEVNULL)
                    archives[url] = extracted
                shutil.copyfile(archives[url] / member, target)
            else:
                download(source, target)
            if sha256(target) != digest:
                parser.exit(1, f"{name}: SHA-256 differs from the manifest\n")
            print(f"{name} ok")
        shutil.copytree(staged, args.destination)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (urllib.error.URLError, TimeoutError) as error:
        print(f"Test ROM download failed: {error}", file=sys.stderr)
        sys.exit(1)