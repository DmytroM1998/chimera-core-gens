#!/usr/bin/env python3
"""Validate a synthetic original-Gens 68000 reference trace."""

import argparse
from contextlib import nullcontext
from pathlib import Path
import sys

from compare_traces import REVISION, TraceError, read_record

HEADER = {
    "type": "header", "format": "gens-m68k-micro-v1", "baseline": REVISION,
    "fixture": "synthetic-bus-irq-1",
}
STEP_KEYS = {
    "type", "step", "phase", "target", "status", "pc", "sr", "odometer",
    "asp", "interrupts", "d", "a", "io", "irq_ack_count", "irq_last_level",
    "events", "ram", "cycles_needed", "cycles_leftover", "io_cycle_counter",
    "xflag", "execinfo", "trace_trickybit",
}
HEX = set("0123456789abcdef")


def integer(value, maximum, name):
    if type(value) is not int or not 0 <= value <= maximum:
        raise TraceError(f"{name}: invalid integer")


def vector(value, length, maximum, name):
    if not isinstance(value, list) or len(value) != length:
        raise TraceError(f"{name}: expected {length} values")
    for item in value:
        integer(item, maximum, name)


def validate(path):
    source = nullcontext(path) if hasattr(path, "read") else Path(path).open("r", encoding="utf-8")
    with source as stream:
        if read_record(stream, "header") != HEADER:
            raise TraceError("invalid reference trace header")
        previous_odometer = 0
        previous_events = []
        phase = 0
        phase_counts = [0, 0]
        previous = None
        count = 0
        while True:
            record = read_record(stream, f"record {count}")
            if record.get("type") == "end":
                if set(record) != {"type", "steps"} or type(record["steps"]) is not int \
                        or record["steps"] != count:
                    raise TraceError("invalid trace end record")
                if stream.read(1):
                    raise TraceError("data follows trace end")
                break
            if set(record) != STEP_KEYS or record["type"] != "step":
                raise TraceError(f"record {count}: invalid step shape")
            if count >= 128:
                raise TraceError("trace exceeds the fixture's step limit")
            if type(record["step"]) is not int or record["step"] != count:
                raise TraceError(f"record {count}: step sequence is not contiguous")
            if type(record["phase"]) is not int or record["phase"] not in (0, 1):
                raise TraceError(f"record {count}: invalid phase")
            if record["phase"] != phase:
                if phase != 0 or record["phase"] != 1 or previous is None:
                    raise TraceError("invalid phase transition")
                check_first_stop(previous)
                phase = 1
            phase_counts[phase] += 1
            for name in ("target", "status", "pc", "odometer", "asp",
                         "irq_ack_count", "irq_last_level", "cycles_needed",
                         "cycles_leftover", "io_cycle_counter"):
                integer(record[name], 0xffffffff, name)
            integer(record["sr"], 0xffff, "sr")
            for name in ("xflag", "execinfo", "trace_trickybit"):
                integer(record[name], 0xff, name)
            if record["status"] != 0x80000000 or record["target"] != previous_odometer + 1 \
                    or record["odometer"] <= previous_odometer:
                raise TraceError(f"record {count}: invalid execution status or cycle progression")
            previous_odometer = record["odometer"]
            vector(record["interrupts"], 8, 0xff, "interrupts")
            vector(record["d"], 8, 0xffffffff, "data registers")
            vector(record["a"], 8, 0xffffffff, "address registers")
            vector(record["io"], 16, 0xff, "I/O bytes")
            events = record["events"]
            if not isinstance(events, list) or len(events) > 512 or \
                    events[:len(previous_events)] != previous_events:
                raise TraceError(f"record {count}: invalid event history")
            for event in events:
                vector(event, 4, 0xffffffff, "event")
                if not 1 <= event[0] <= 11:
                    raise TraceError(f"record {count}: invalid event type")
            previous_events = events
            ram = record["ram"]
            if not isinstance(ram, str) or len(ram) != 65536 * 2 or not set(ram) <= HEX:
                raise TraceError(f"record {count}: invalid RAM image")
            previous = record
            count += 1
        if phase != 1 or phase_counts[0] < 9 or phase_counts[1] < 4 or previous is None:
            raise TraceError("trace is missing fixture instructions or interrupt handling")
        check_final_stop(previous)
        event_types = {event[0] for event in previous_events}
        if not {1, 8, 9, 10, 11} <= event_types:
            raise TraceError("trace is missing execution hooks or bus callbacks")
        return count


def check_first_stop(record):
    if record["pc"] != 0x12c or record["d"][0:2] != [8, 3] or \
            record["d"][2] & 0xff != 0x5a or record["d"][3] & 0xffff != 0xa55a or \
            record["io"][0] != 0x5a or record["io"][2:4] != [0xa5, 0x5a] or \
            record["ram"][:8] != "00000008" or not record["interrupts"][0] & 0x10:
        raise TraceError("first STOP does not match the synthetic fixture")


def check_final_stop(record):
    if record["pc"] != 0x132 or record["d"][4:6] != [42, 7] or \
            record["a"][7] != 0xff1000 or record["irq_ack_count"] != 1 or \
            record["irq_last_level"] != 4 or not record["interrupts"][0] & 0x10:
        raise TraceError("final STOP does not match the interrupt fixture")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        count = validate(args.trace)
    except (OSError, UnicodeError, TraceError, ValueError, RecursionError) as error:
        print(f"68000 trace validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {count} synthetic 68000 reference steps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
