# Checking GMV input alignment

`tools\check_gmv_trace.py` checks a test trace against the frame inputs in a
GMV fixture from the pinned Gens revision. It prints a pass or failure and
does not write a movie, convert a file, or import anything into Chimera.
The emulator trace producers are not implemented yet.

```sh
python3 tools/check_gmv_trace.py fixture.gmv imported-replay.jsonl
```

The check supports uncompressed `Gens Movie TEST9` and `Gens Movie TESTA`
files with a power start, no savestate requirement, complete three-byte
frames, and known 3- or 6-button controller configuration. It rejects
incomplete files, zero-frame fixtures, savestate-required movies, other
versions, and traces with reset events. This is intentionally narrower than
what the original frontend may accept. A rejected fixture is not a
desynchronization result.

The decoder follows `Gens-rr\src\movie.cpp` at the pinned revision:

- Every frame starts at offset `64 + 3 * frame_index`.
- Each bit is active-low. A zero is pressed.
- Bits 0 through 7 are Up, Down, Left, Right, A, B, C, and Start.
- In ordinary two-player mode the first two bytes are player 1 and player 2.
  The third byte contains X, Y, Z, and Mode for player 1 in its low nibble,
  followed by those buttons for player 2 in its high nibble.
- In the three-player mode all three bytes are eight-button tracks for
  Gens controller 1, 1B, and 1C. `PlaySubMovie` forces 1B and 1C to
  three-button controllers. The first controller's X, Y, Z, and Mode are
  not supplied by the movie playback branch. This checker therefore rejects
  three-player movies configured with a six-button first pad rather than
  guessing their values.

The trace format is documented in `TRACE-FORMAT.md`. For this check, each
frame's `input` object must contain exactly `p1`, `p2`, `p3`, and `reset`.
The players are nonnegative 12-bit pressed masks. Bits 0 through 7 use the
order above and bits 8 through 11 are X, Y, Z, and Mode. A set bit means
pressed. `p3` is zero in two-player mode, and all extended bits are zero in
the supported three-player mode. `reset` must be false because this GMV
layout does not supply a reset event.

`input_sha256` covers the entire sequence of these input objects encoded as
seven bytes per frame: three unsigned 16-bit little-endian player masks,
then one byte for reset (`0` or `1`). It does not include JSON or frame
indices. The trace header's `movie_sha256` must match the complete GMV.

The settings object must have:

- `region`: `PAL` or `NTSC` matching the refresh flag in a TESTA header.
  TEST9 has no such flag and uses the trace's explicitly chosen rate.
  The TESTA flag is movie metadata. This check does not prove that original
  playback actually used that refresh rate or select a console country.
- `controller_topology`: `gens-two-pad` or `gens-team-player`.
- `controller_types`: the original physical controller types. Two-player
  mode uses `{"p1": 3, "p2": 6}` with the actual header values. Three-player
  mode additionally includes `"p1b": 3` and `"p1c": 3`.
- `start_mode`: `power`.
- `reset_schedule`: an empty array.
- `clear_sram`: a boolean recording the chosen original playback option.

The check covers only movie-driven input levels. In three-player mode,
controller 2 on the physical console port and the unused extended levels
are not supplied by the movie branch. The reference and trace producer must
account for their initial state separately. The checker cannot establish
ROM identity, initial machine state, emulator behavior, lag accuracy, or
completion of the TAS. Matching inputs alone are not proof of movie sync.
For power-start playback, the original frontend reloads the ROM and can
clear SRAM afterwards. Record the actual playback option and initial
machine state rather than treating an ordinary soft reset as equivalent.
