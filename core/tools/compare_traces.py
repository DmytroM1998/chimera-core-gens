#!/usr/bin/env python3
"""Compare canonical original and ported Gens frame traces."""

import argparse
import json
from pathlib import Path
import re
import sys

REVISION = "b948995d15034983cbf5562610586d93e0b25a9b"
HASH = re.compile(r"[0-9a-f]{64}\Z")
STATE_KEYS = {"m68k", "z80", "scheduler", "memory", "vdp", "controllers", "audio"}
HEADER_KEYS = {
    "type", "format", "baseline", "producer", "rom_sha256",
    "movie_sha256", "input_sha256", "initial_state_sha256", "settings",
    "expected_frames",
}
FRAME_KEYS = {"type", "index", "input", "lag", "state", "video_sha256", "audio_sha256"}
MAX_LINE = 1024 * 1024


class TraceError(Exception):
    pass


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate key {key!r}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"non-finite number {value}")


def read_record(stream, location):
    line = stream.readline(MAX_LINE + 1)
    if not line:
        raise TraceError(f"{location}: unexpected end of file")
    if len(line) > MAX_LINE:
        raise TraceError(f"{location}: record exceeds {MAX_LINE} characters")
    try:
        record = json.loads(line, object_pairs_hook=no_duplicates,
                            parse_constant=reject_constant)
    except (ValueError, RecursionError) as error:
        raise TraceError(f"{location}: invalid JSON: {error}") from error
    if not isinstance(record, dict):
        raise TraceError(f"{location}: expected an object")
    return record


def check_keys(record, keys, location):
    if set(record) != keys:
        raise TraceError(f"{location}: expected keys {', '.join(sorted(keys))}")


def check_hash(value, location):
    if not isinstance(value, str) or not HASH.fullmatch(value):
        raise TraceError(f"{location}: expected a lowercase SHA-256 digest")


def check_header(header, location):
    check_keys(header, HEADER_KEYS, location)
    if header["type"] != "header" or header["format"] != "gens-sync-trace-v1":
        raise TraceError(f"{location}: unsupported trace header")
    if header["baseline"] != REVISION:
        raise TraceError(f"{location}: incorrect Gens baseline")
    if not isinstance(header["producer"], str) or not header["producer"].strip():
        raise TraceError(f"{location}: producer must be a nonempty string")
    for key in ("rom_sha256", "movie_sha256", "input_sha256", "initial_state_sha256"):
        check_hash(header[key], f"{location}.{key}")
    if not isinstance(header["settings"], dict) or not header["settings"]:
        raise TraceError(f"{location}: settings must be a nonempty object")
    settings = header["settings"]
    if settings.get("region") not in ("NTSC", "PAL"):
        raise TraceError(f"{location}: settings.region must be NTSC or PAL")
    topology = settings.get("controller_topology")
    if not isinstance(topology, str) or not topology.strip():
        raise TraceError(f"{location}: settings.controller_topology must be nonempty")
    if settings.get("start_mode") not in ("power", "reset"):
        raise TraceError(f"{location}: unsupported settings.start_mode")
    if type(header["expected_frames"]) is not int or header["expected_frames"] <= 0:
        raise TraceError(f"{location}: expected_frames must be a positive integer")


def check_frame(record, index, location):
    check_keys(record, FRAME_KEYS, location)
    if record["type"] != "frame" or type(record["index"]) is not int or record["index"] != index:
        raise TraceError(f"{location}: expected frame {index}")
    if not isinstance(record["input"], dict) or not record["input"]:
        raise TraceError(f"{location}: input must be a nonempty object")
    if type(record["lag"]) is not bool:
        raise TraceError(f"{location}: lag must be a boolean")
    state = record["state"]
    if not isinstance(state, dict):
        raise TraceError(f"{location}: state must be an object")
    check_keys(state, STATE_KEYS, f"{location}.state")
    for key, value in state.items():
        check_hash(value, f"{location}.state.{key}")
    for key in ("video_sha256", "audio_sha256"):
        check_hash(record[key], f"{location}.{key}")


def check_end(stream, count, location):
    record = read_record(stream, location)
    check_keys(record, {"type", "frames"}, location)
    if record["type"] != "end" or type(record["frames"]) is not int or record["frames"] != count:
        raise TraceError(f"{location}: expected end record for {count} frames")
    if stream.read(1):
        raise TraceError(f"{location}: data follows the end record")


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=True,
                      separators=(",", ":"), allow_nan=False)


def compare(original_path, ported_path):
    if Path(original_path).samefile(ported_path):
        raise TraceError("original and ported traces refer to the same file")
    with Path(original_path).open("r", encoding="utf-8") as original, \
            Path(ported_path).open("r", encoding="utf-8") as ported:
        left = read_record(original, "original header")
        right = read_record(ported, "ported header")
        check_header(left, "original header")
        check_header(right, "ported header")
        if left["producer"] == right["producer"]:
            raise TraceError("original and ported producer names must differ")
        left_metadata = {key: value for key, value in left.items() if key != "producer"}
        right_metadata = {key: value for key, value in right.items() if key != "producer"}
        if canonical(left_metadata) != canonical(right_metadata):
            raise TraceError("trace metadata differs, so playback conditions or input do not match")
        count = left["expected_frames"]
        for index in range(count):
            left_frame = read_record(original, f"original frame {index}")
            right_frame = read_record(ported, f"ported frame {index}")
            check_frame(left_frame, index, f"original frame {index}")
            check_frame(right_frame, index, f"ported frame {index}")
            if canonical(left_frame) != canonical(right_frame):
                differences = sorted(key for key in FRAME_KEYS
                                     if canonical(left_frame[key]) != canonical(right_frame[key]))
                raise TraceError(f"frame {index} differs: {', '.join(differences)}")
        check_end(original, count, "original end")
        check_end(ported, count, "ported end")
        return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("ported", type=Path)
    args = parser.parse_args()
    try:
        count = compare(args.original, args.ported)
    except (OSError, UnicodeError, TraceError, ValueError, RecursionError) as error:
        print(f"Trace comparison failed: {error}", file=sys.stderr)
        return 1
    print(f"Compared {count} frames with matching canonical trace records")
    return 0


if __name__ == "__main__":
    sys.exit(main())
