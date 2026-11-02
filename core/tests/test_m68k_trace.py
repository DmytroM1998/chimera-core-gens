import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from check_m68k_trace import HEADER, TraceError, validate
from compare_m68k_traces import compare


def fixture():
    events = [[kind, 0, 0, 0] for kind in (1, 8, 9, 10, 11)]
    records = [HEADER]
    for step in range(13):
        phase = 0 if step < 9 else 1
        data = [8, 3, 0x5a, 0xa55a, 0, 0, 0, 0]
        if step == 12:
            data[4:6] = [42, 7]
        records.append({
            "type": "step", "step": step, "phase": phase,
            "target": step * 4 + 1, "status": 0x80000000,
            "pc": 0x12c if phase == 0 else 0x132, "sr": 0x2000,
            "odometer": (step + 1) * 4, "asp": 0,
            "cycles_needed": 0, "cycles_leftover": 0, "io_cycle_counter": 0xffffffff,
            "xflag": 0, "execinfo": 0, "trace_trickybit": 0,
            "interrupts": [0x10] + [0] * 7, "d": data,
            "a": [0] * 7 + [0xff1000],
            "io": [0x5a, 0, 0xa5, 0x5a] + [0] * 12,
            "irq_ack_count": 0 if phase == 0 else 1,
            "irq_last_level": 0 if phase == 0 else 4,
            "events": copy.deepcopy(events),
            "ram": "00000008" + "0" * (65536 * 2 - 8),
        })
    records.append({"type": "end", "steps": 13})
    return records


class M68kTraceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "trace.jsonl"
        self.other = Path(self.tmp.name) / "other.jsonl"
        self.records = fixture()

    def write(self):
        self.path.write_text("".join(json.dumps(record) + "\n" for record in self.records),
                             encoding="utf-8")

    def test_valid_trace(self):
        self.write()
        self.assertEqual(validate(self.path), 13)

    def test_truncation_and_duplicate_step(self):
        self.records.pop()
        self.write()
        with self.assertRaisesRegex(TraceError, "unexpected end"):
            validate(self.path)
        self.records = fixture()
        self.records[2]["step"] = 0
        self.write()
        with self.assertRaisesRegex(TraceError, "not contiguous"):
            validate(self.path)

    def test_missing_bus_and_bad_ram(self):
        for record in self.records[1:-1]:
            record["events"] = [[1, 0, 0, 0]]
        self.write()
        with self.assertRaisesRegex(TraceError, "bus callbacks"):
            validate(self.path)
        self.records = fixture()
        self.records[1]["ram"] = "00"
        self.write()
        with self.assertRaisesRegex(TraceError, "RAM image"):
            validate(self.path)

    def test_cycle_and_stop_changes(self):
        self.records[2]["target"] = 3
        self.write()
        with self.assertRaisesRegex(TraceError, "cycle progression"):
            validate(self.path)
        self.records = fixture()
        self.records[9]["d"][0] = 9
        self.write()
        with self.assertRaisesRegex(TraceError, "first STOP"):
            validate(self.path)
        self.records = fixture()
        self.records[-2]["irq_last_level"] = 3
        self.write()
        with self.assertRaisesRegex(TraceError, "final STOP"):
            validate(self.path)

    def test_rejects_empty_and_boolean_numbers(self):
        self.path.write_text("", encoding="utf-8")
        with self.assertRaisesRegex(TraceError, "unexpected end"):
            validate(self.path)
        self.records[1]["d"][0] = True
        self.write()
        with self.assertRaisesRegex(TraceError, "invalid integer"):
            validate(self.path)

    def test_comparison_detects_register_and_ram_changes(self):
        self.write()
        self.other.write_bytes(self.path.read_bytes())
        self.assertEqual(compare(self.path, self.other), 13)
        with self.assertRaisesRegex(TraceError, "same file"):
            compare(self.path, self.path)
        self.records[4]["d"][0] = 9
        self.other.write_text("".join(json.dumps(record) + "\n" for record in self.records),
                              encoding="utf-8")
        with self.assertRaisesRegex(TraceError, r"step 3 differs: d\[0\]"):
            compare(self.path, self.other)
        self.records = fixture()
        ram = self.records[4]["ram"]
        self.records[4]["ram"] = ram[:64] + "ff" + ram[66:]
        self.other.write_text("".join(json.dumps(record) + "\n" for record in self.records),
                              encoding="utf-8")
        with self.assertRaisesRegex(TraceError, "step 3 differs: RAM byte 0x0020"):
            compare(self.path, self.other)


if __name__ == "__main__":
    unittest.main()
