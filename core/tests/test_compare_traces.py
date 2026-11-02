import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from compare_traces import REVISION, STATE_KEYS, TraceError, compare


def records(producer):
    digest = "a" * 64
    header = {
        "type": "header", "format": "gens-sync-trace-v1",
        "baseline": REVISION, "producer": producer,
        "rom_sha256": digest, "movie_sha256": digest, "input_sha256": digest,
        "initial_state_sha256": digest, "settings": {
            "region": "NTSC", "controller_topology": "team-player",
            "start_mode": "power", "players": 3,
        },
        "expected_frames": 2,
    }
    frame = {
        "type": "frame", "index": 0, "input": {"p1": 0, "p2": 1, "p3": 2},
        "lag": False, "state": {key: digest for key in STATE_KEYS},
        "video_sha256": digest, "audio_sha256": digest,
    }
    second = dict(frame, index=1)
    return [header, frame, second, {"type": "end", "frames": 2}]


def write(path, items):
    path.write_text("".join(json.dumps(item) + "\n" for item in items), encoding="utf-8")


class TraceComparisonTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.left = Path(self.tmp.name) / "original.jsonl"
        self.right = Path(self.tmp.name) / "ported.jsonl"
        self.a = records("original")
        self.b = records("port")
        write(self.left, self.a)
        write(self.right, self.b)

    def test_equal(self):
        self.assertEqual(compare(self.left, self.right), 2)

    def test_self_comparison_is_rejected(self):
        with self.assertRaisesRegex(TraceError, "same file"):
            compare(self.left, self.left)
        self.b[0]["producer"] = "original"
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "producer names must differ"):
            compare(self.left, self.right)

    def test_mismatched_metadata(self):
        self.b[0]["settings"]["region"] = "PAL"
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "metadata differs"):
            compare(self.left, self.right)

    def test_frame_input_and_type_differences(self):
        self.b[1]["input"]["p1"] = True
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "frame 0 differs: input"):
            compare(self.left, self.right)

    def test_machine_difference(self):
        self.b[2]["state"] = dict(self.b[2]["state"], m68k="b" * 64)
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "frame 1 differs: state"):
            compare(self.left, self.right)

    def test_truncation_even_when_both_traces_match(self):
        write(self.left, self.a[:2])
        write(self.right, self.b[:2])
        with self.assertRaisesRegex(TraceError, "unexpected end"):
            compare(self.left, self.right)

    def test_missing_component(self):
        del self.b[1]["state"]["z80"]
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "expected keys"):
            compare(self.left, self.right)

    def test_duplicate_key_and_trailing_data(self):
        self.right.write_text('{"type":"header","type":"header"}\n', encoding="utf-8")
        with self.assertRaisesRegex(TraceError, "duplicate key"):
            compare(self.left, self.right)
        write(self.right, self.b)
        with self.right.open("a", encoding="utf-8") as stream:
            stream.write("{}\n")
        with self.assertRaisesRegex(TraceError, "data follows"):
            compare(self.left, self.right)

    def test_out_of_sequence_and_zero_frames(self):
        self.b[2]["index"] = 2
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "expected frame 1"):
            compare(self.left, self.right)
        self.b = records("port")
        self.b[0]["expected_frames"] = 0
        write(self.right, self.b)
        with self.assertRaisesRegex(TraceError, "positive integer"):
            compare(self.left, self.right)


if __name__ == "__main__":
    unittest.main()
