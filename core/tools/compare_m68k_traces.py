#!/usr/bin/env python3
"""Compare validated original and ported 68000 micro-reference traces."""

import argparse
import io
from pathlib import Path
import sys

from check_m68k_trace import validate
from compare_traces import TraceError, canonical, read_record

MAX_BYTES = 32 * 1024 * 1024


def snapshot(path):
    with Path(path).open("rb") as stream:
        content = stream.read(MAX_BYTES + 1)
    if len(content) > MAX_BYTES:
        raise TraceError(f"{path}: trace exceeds the size limit")
    return io.StringIO(content.decode("utf-8"))


def difference(left, right):
    for key in sorted(left):
        if canonical(left[key]) == canonical(right[key]):
            continue
        if key == "ram":
            before, after = bytes.fromhex(left[key]), bytes.fromhex(right[key])
            index = next(i for i, pair in enumerate(zip(before, after)) if pair[0] != pair[1])
            return f"RAM byte 0x{index:04x}"
        if key in ("d", "a", "io", "interrupts", "events"):
            a, b = left[key], right[key]
            for index in range(min(len(a), len(b))):
                if canonical(a[index]) != canonical(b[index]):
                    return f"{key}[{index}]"
            return f"{key} length"
        return key
    raise TraceError("records differ but no field was identified")


def compare(original_path, ported_path):
    if Path(original_path).samefile(ported_path):
        raise TraceError("original and ported traces refer to the same file")
    with snapshot(original_path) as original, snapshot(ported_path) as ported:
        original_count = validate(original)
        ported_count = validate(ported)
        original.seek(0)
        ported.seek(0)
        if canonical(read_record(original, "original header")) != \
                canonical(read_record(ported, "ported header")):
            raise TraceError("trace headers differ")
        for index in range(min(original_count, ported_count)):
            left = read_record(original, f"original step {index}")
            right = read_record(ported, f"ported step {index}")
            if canonical(left) != canonical(right):
                raise TraceError(f"step {index} differs: {difference(left, right)}")
        if original_count != ported_count:
            raise TraceError(f"step count differs: {original_count} versus {ported_count}")
        return original_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("ported", type=Path)
    args = parser.parse_args()
    try:
        count = compare(args.original, args.ported)
    except (OSError, UnicodeError, TraceError, ValueError, RecursionError) as error:
        print(f"68000 trace comparison failed: {error}", file=sys.stderr)
        return 1
    print(f"Matched {count} synthetic 68000 steps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
