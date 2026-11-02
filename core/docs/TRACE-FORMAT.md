# TAS synchronization traces

`tools\compare_traces.py` compares line-delimited JSON traces from an
original Gens reference and the port. It is an acceptance aid, not a movie
importer. Neither trace producer exists yet. The trace schema is provisional
until those producers define and validate the canonical state encoding.

Run:

```sh
python3 tools/compare_traces.py original.jsonl ported.jsonl
```

The tool rejects incompatible metadata, missing or additional fields,
duplicate JSON keys, malformed hashes, non-contiguous frame indices,
truncation, self-comparison, identical producer names, and data following
the end record. It reports the first different frame. It requires a
positive declared frame count and matching end records even when both traces
are identically truncated.

## Records

The first line is an object with exactly these fields:

- `type`: `header`
- `format`: `gens-sync-trace-v1`
- `baseline`: `b948995d15034983cbf5562610586d93e0b25a9b`
- `producer`: a nonempty descriptive string. It must differ between the two
  files and is the only header field allowed to do so.
- `rom_sha256` and `movie_sha256`: lowercase SHA-256 digests of the
  exact ROM and original movie fixture.
- `input_sha256`: lowercase SHA-256 of the entire normalized imported input
  sequence. Both producers must use the same documented encoding.
- `initial_state_sha256`: lowercase SHA-256 of the canonical machine state
  at the beginning of replay. Do not hash pointer-containing host structs.
- `settings`: an object including `region` (`NTSC` or `PAL`),
  `controller_topology` (a nonempty string), and `start_mode` (`power` or
  `reset`). Include all other machine-affecting settings and reset schedules
  needed to reproduce the fixture.
- `expected_frames`: the positive number of frames in the full fixture, not
  the number a failed or interrupted run managed to emit.

  For the narrower test-only GMV input alignment check, the required input
  encoding and hash are defined in `GMV-INPUT-CHECK.md`.

Each frame line has exactly `type`, `index`, `input`, `lag`, `state`,
`video_sha256`, and `audio_sha256`. `type` is `frame`. `index` starts at
zero and increases by one. `input` is a nonempty object containing all
player levels and reset events using the same normalized mapping as the
input digest. `lag` is a boolean. The output hashes cover the canonical
video and audio output for that frame.

`state` has exactly `m68k`, `z80`, `scheduler`, `memory`, `vdp`,
`controllers`, and `audio`, each a lowercase SHA-256 of the corresponding
canonical emulated state. Producers must agree on field coverage, encoding,
and capture timing, including cycle counters and device state. Hashing only
the screen or hashing empty placeholders does not meet that requirement.

The last line is `{"type":"end","frames":N}`, where `N` equals
`expected_frames`. No further data, including blank lines, is accepted.
Savestate-start movies remain outside the initial compatibility gate unless
an equivalent reproducible starting state is established.

Matching trace records do not by themselves prove that either producer
captures every relevant field, that the imported inputs were decoded
correctly, or that a game reached its intended milestone. Those properties
must be established separately against trusted original playback.
