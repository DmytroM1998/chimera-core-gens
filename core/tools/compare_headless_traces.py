#!/usr/bin/env python3
"""Compare two headless traces (tests/gens_headless_trace.h) block by block.

Exits nonzero and names the first differing frame and block when they differ.
With --require-changing, it also fails when a named block never changes in the
expected trace. For example, a constant "z80" block means the Z80 never ran.
"""

import argparse
import sys


def load(path, skip):
    blocks = {}
    with open(path) as f:
        for line in f:
            frame, name, digest = line.split()
            if name not in skip:
                blocks[(int(frame), name)] = digest
    return blocks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected")
    parser.add_argument("actual")
    parser.add_argument("--skip", default="", help="comma-separated block names to ignore")
    parser.add_argument("--require-changing", default="",
                        help="comma-separated block names that must change in the expected trace")
    args = parser.parse_args()
    skip = set(filter(None, args.skip.split(",")))
    expected, actual = load(args.expected, skip), load(args.actual, skip)
    if not expected:
        print("the expected trace is empty")
        return 1
    for name in filter(None, args.require_changing.split(",")):
        values = {digest for (_, block), digest in expected.items() if block == name}
        if len(values) < 2:
            print(f"the {name} block never changes in the expected trace")
            return 1
    keys = sorted(set(expected) | set(actual))
    different = [key for key in keys if expected.get(key) != actual.get(key)]
    frames = max(key[0] for key in keys) + 1
    if different:
        frame, name = different[0]
        print(f"{frames} frames, {len(different)} of {len(keys)} blocks differ, first at frame {frame} ({name})")
        return 1
    print(f"{frames} frames, all {len(keys)} blocks match")
    return 0


if __name__ == "__main__":
    sys.exit(main())