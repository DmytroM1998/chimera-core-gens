#!/usr/bin/env python3
"""Check test-only GMV input alignment against a canonical replay trace."""

import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys

from compare_traces import TraceError, check_end, check_frame, check_header, read_record

MAGIC_A = b"Gens Movie TESTA"
MAGIC_9 = b"Gens Movie TEST9"
HEADER_SIZE = 64
FRAME_SIZE = 3


def decode_frame(data, triple):
    a, b, extra = data
    if triple:
        return {
            "p1": (~a) & 0xff, "p2": (~b) & 0xff,
            "p3": (~extra) & 0xff, "reset": False,
        }
    return {
        "p1": ((~a) & 0xff) | (((~extra) & 0x0f) << 8),
        "p2": ((~b) & 0xff) | (((~extra >> 4) & 0x0f) << 8),
        "p3": 0,
        "reset": False,
    }


def encode_input(record):
    if not isinstance(record, dict) or set(record) != {"p1", "p2", "p3", "reset"}:
        raise TraceError("input must contain exactly p1, p2, p3, and reset")
    for key in ("p1", "p2", "p3"):
        if type(record[key]) is not int or not 0 <= record[key] <= 0xfff:
            raise TraceError(f"input.{key} must be a 12-bit integer")
    if type(record["reset"]) is not bool:
        raise TraceError("input.reset must be a boolean")
    return struct.pack("<HHHB", record["p1"], record["p2"], record["p3"],
                       int(record["reset"]))


def check(movie_path, trace_path):
    with Path(movie_path).open("rb") as movie, \
            Path(trace_path).open("r", encoding="utf-8") as trace:
        size = os.fstat(movie.fileno()).st_size
        if size < HEADER_SIZE + FRAME_SIZE or (size - HEADER_SIZE) % FRAME_SIZE:
            raise TraceError("GMV must contain a complete header and at least one full frame")
        header = movie.read(HEADER_SIZE)
        if len(header) != HEADER_SIZE or header[:16] not in (MAGIC_A, MAGIC_9):
            raise TraceError("only Gens Movie TEST9 and TESTA fixtures are supported")
        if header[20] not in (ord("3"), ord("6")) or header[21] not in (ord("3"), ord("6")):
            raise TraceError("GMV controller types must be 3 or 6")
        flags = header[22] if header[:16] == MAGIC_A else 0
        if flags & 0x40:
            raise TraceError("savestate-start GMV fixtures are not supported")
        triple = bool(flags & 0x20)
        p1_type, p2_type = header[20] - ord("0"), header[21] - ord("0")
        if triple and p1_type != 3:
            raise TraceError("three-player GMV with a six-button first pad has unrecorded extra levels")
        count = (size - HEADER_SIZE) // FRAME_SIZE
        trace_header = read_record(trace, "trace header")
        check_header(trace_header, "trace header")
        settings = trace_header["settings"]
        if trace_header["expected_frames"] != count:
            raise TraceError("GMV and trace frame counts differ")
        region = ("PAL" if flags & 0x80 else "NTSC") if header[:16] == MAGIC_A else None
        topology = "gens-team-player" if triple else "gens-two-pad"
        controllers = {"p1": p1_type, "p2": p2_type}
        if triple:
            controllers.update({"p1b": 3, "p1c": 3})
        if ((region is not None and settings.get("region") != region)
                or settings.get("controller_topology") != topology):
            raise TraceError("GMV region or topology differs from trace settings")
        actual_controllers = settings.get("controller_types")
        if (not isinstance(actual_controllers, dict)
                or set(actual_controllers) != set(controllers)
                or any(type(actual_controllers[key]) is not int
                       or actual_controllers[key] != value
                       for key, value in controllers.items())):
            raise TraceError("GMV controller types differ from trace settings")
        if settings.get("start_mode") != "power" or settings.get("reset_schedule") != []:
            raise TraceError("this check requires power start and no reset events")
        if type(settings.get("clear_sram")) is not bool:
            raise TraceError("trace settings must state whether SRAM was cleared")

        movie_hash = hashlib.sha256(header)
        input_hash = hashlib.sha256()
        for index in range(count):
            data = movie.read(FRAME_SIZE)
            if len(data) != FRAME_SIZE:
                raise TraceError(f"GMV truncated at frame {index}")
            movie_hash.update(data)
            expected = decode_frame(data, triple)
            frame = read_record(trace, f"trace frame {index}")
            check_frame(frame, index, f"trace frame {index}")
            normalized = encode_input(frame["input"])
            if frame["input"] != expected:
                raise TraceError(f"GMV input differs from trace at frame {index}")
            input_hash.update(normalized)
        if movie.read(1):
            raise TraceError("GMV changed while it was being read")
        check_end(trace, count, "trace end")
        if movie_hash.hexdigest() != trace_header["movie_sha256"]:
            raise TraceError("GMV SHA-256 differs from trace metadata")
        if input_hash.hexdigest() != trace_header["input_sha256"]:
            raise TraceError("normalized input SHA-256 differs from trace metadata")
        return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("movie", type=Path)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        count = check(args.movie, args.trace)
    except (OSError, UnicodeError, TraceError, ValueError, RecursionError) as error:
        print(f"GMV input check failed: {error}", file=sys.stderr)
        return 1
    print(f"Verified GMV input alignment for {count} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
