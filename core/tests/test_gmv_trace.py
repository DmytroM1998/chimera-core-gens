import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from check_gmv_trace import TraceError, check, decode_frame, encode_input
from compare_traces import REVISION, STATE_KEYS


def fixture(frames, inputs, triple=False, flags=0, p1=3, p2=3, version="A"):
    header = bytearray(64)
    header[:16] = f"Gens Movie TEST{version}".encode("ascii")
    header[20:22] = f"{p1}{p2}".encode("ascii")
    header[22] = flags | (0x20 if triple else 0)
    movie = bytes(header) + b"".join(frames)
    digest = "a" * 64
    controllers = {"p1": p1, "p2": p2}
    if triple:
        controllers.update({"p1b": 3, "p1c": 3})
    metadata = {
        "type": "header", "format": "gens-sync-trace-v1", "baseline": REVISION,
        "producer": "ported", "rom_sha256": digest,
        "movie_sha256": hashlib.sha256(movie).hexdigest(),
        "input_sha256": hashlib.sha256(b"".join(
            struct.pack("<HHHB", i["p1"], i["p2"], i["p3"], i["reset"])
            for i in inputs)).hexdigest(),
        "initial_state_sha256": digest, "expected_frames": len(frames),
        "settings": {
            "region": "PAL" if flags & 0x80 else "NTSC",
            "controller_topology": "gens-team-player" if triple else "gens-two-pad",
            "controller_types": controllers, "start_mode": "power",
            "reset_schedule": [], "clear_sram": True,
        },
    }
    state = {key: digest for key in STATE_KEYS}
    trace = [metadata]
    for index, input_ in enumerate(inputs):
        trace.append({"type": "frame", "index": index, "input": input_,
                      "lag": False, "state": state,
                      "video_sha256": digest, "audio_sha256": digest})
    trace.append({"type": "end", "frames": len(frames)})
    return movie, trace


class GmvTraceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.movie_path = Path(self.tmp.name) / "movie.gmv"
        self.trace_path = Path(self.tmp.name) / "trace.jsonl"

    def write(self, movie, trace):
        self.movie_path.write_bytes(movie)
        self.trace_path.write_text("".join(json.dumps(r) + "\n" for r in trace),
                                   encoding="utf-8")

    def test_two_pads_six_button_and_active_low(self):
        frames = [bytes.fromhex("ff ff ff"), bytes.fromhex("ee dd a5")]
        inputs = [
            {"p1": 0, "p2": 0, "p3": 0, "reset": False},
            {"p1": 0xa11, "p2": 0x522, "p3": 0, "reset": False},
        ]
        movie, trace = fixture(frames, inputs, p1=6, p2=6, flags=0x80)
        self.write(movie, trace)
        self.assertEqual(check(self.movie_path, self.trace_path), 2)

    def test_team_player_routes_three_tracks(self):
        frames = [bytes.fromhex("fe fd fb")]
        inputs = [{"p1": 1, "p2": 2, "p3": 4, "reset": False}]
        movie, trace = fixture(frames, inputs, triple=True)
        self.write(movie, trace)
        self.assertEqual(check(self.movie_path, self.trace_path), 1)

    def test_test9_uses_trace_region_not_reserved_flags(self):
        inputs = [{"p1": 0, "p2": 0, "p3": 0, "reset": False}]
        movie, trace = fixture([b"\xff\xff\xff"], inputs, flags=0xe0, version="9")
        trace[0]["settings"]["region"] = "PAL"
        self.write(movie, trace)
        self.assertEqual(check(self.movie_path, self.trace_path), 1)

    def test_each_basic_and_extra_button_bit(self):
        for bit in range(8):
            data = bytes([0xff ^ (1 << bit), 0xff, 0xff])
            self.assertEqual(decode_frame(data, False)["p1"], 1 << bit)
            data = bytes([0xff, 0xff ^ (1 << bit), 0xff])
            self.assertEqual(decode_frame(data, False)["p2"], 1 << bit)
            self.assertEqual(decode_frame(data, True)["p2"], 1 << bit)
            data = bytes([0xff, 0xff, 0xff ^ (1 << bit)])
            self.assertEqual(decode_frame(data, True)["p3"], 1 << bit)
            player = "p1" if bit < 4 else "p2"
            self.assertEqual(decode_frame(data, False)[player], 1 << (8 + bit % 4))

    def test_mismatch_and_reset_are_rejected(self):
        movie, trace = fixture([b"\xff\xff\xff"],
                               [{"p1": 0, "p2": 0, "p3": 0, "reset": False}])
        trace[1]["input"]["p1"] = 1
        self.write(movie, trace)
        with self.assertRaisesRegex(TraceError, "differs from trace at frame 0"):
            check(self.movie_path, self.trace_path)
        trace[1]["input"]["p1"] = 0
        trace[1]["input"]["reset"] = True
        self.write(movie, trace)
        with self.assertRaisesRegex(TraceError, "differs from trace at frame 0"):
            check(self.movie_path, self.trace_path)

    def test_wrong_hash_and_incomplete_frame(self):
        movie, trace = fixture([b"\xff\xff\xff"],
                               [{"p1": 0, "p2": 0, "p3": 0, "reset": False}])
        trace[0]["input_sha256"] = "0" * 64
        self.write(movie, trace)
        with self.assertRaisesRegex(TraceError, "normalized input SHA-256"):
            check(self.movie_path, self.trace_path)
        self.write(movie + b"\x00", trace)
        with self.assertRaisesRegex(TraceError, "full frame"):
            check(self.movie_path, self.trace_path)

    def test_savestate_and_unrecorded_six_button_levels(self):
        inputs = [{"p1": 0, "p2": 0, "p3": 0, "reset": False}]
        for options, message in [
            ({"flags": 0x40}, "savestate-start"),
            ({"triple": True, "p1": 6}, "unrecorded extra levels"),
        ]:
            movie, trace = fixture([b"\xff\xff\xff"], inputs, **options)
            self.write(movie, trace)
            with self.assertRaisesRegex(TraceError, message):
                check(self.movie_path, self.trace_path)

    def test_encoded_input_rejects_booleans_as_masks(self):
        with self.assertRaisesRegex(TraceError, "12-bit integer"):
            encode_input({"p1": False, "p2": 0, "p3": 0, "reset": False})

    def test_metadata_mismatch_and_float_controller_type(self):
        movie, trace = fixture([b"\xff\xff\xff"],
                               [{"p1": 0, "p2": 0, "p3": 0, "reset": False}])
        trace[0]["settings"]["region"] = "PAL"
        self.write(movie, trace)
        with self.assertRaisesRegex(TraceError, "region or topology"):
            check(self.movie_path, self.trace_path)
        trace[0]["settings"]["region"] = "NTSC"
        trace[0]["settings"]["controller_types"]["p1"] = 3.0
        self.write(movie, trace)
        with self.assertRaisesRegex(TraceError, "controller types"):
            check(self.movie_path, self.trace_path)

    def test_modified_header_is_rejected_by_movie_hash(self):
        movie, trace = fixture([b"\xff\xff\xff"],
                               [{"p1": 0, "p2": 0, "p3": 0, "reset": False}])
        altered = bytearray(movie)
        altered[16] = 1
        self.write(bytes(altered), trace)
        with self.assertRaisesRegex(TraceError, "GMV SHA-256"):
            check(self.movie_path, self.trace_path)

    def test_command_reports_success_and_failure(self):
        movie, trace = fixture([b"\xff\xff\xff"],
                               [{"p1": 0, "p2": 0, "p3": 0, "reset": False}])
        self.write(movie, trace)
        tool = Path(__file__).resolve().parent.parent / "tools" / "check_gmv_trace.py"
        command = [sys.executable, str(tool), str(self.movie_path), str(self.trace_path)]
        valid = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        trace[1]["input"]["p1"] = 1
        self.write(movie, trace)
        invalid = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("frame 0", invalid.stderr)


if __name__ == "__main__":
    unittest.main()
