# PCM output conversion

`waterbox\gens_pcm.c` converts the original signed 32-bit mixer planes to
interleaved signed 16-bit stereo output. It follows the pinned
`G_dsound.cpp` and `Misc.asm` conversion paths without DirectSound, host
device access, or CPU-feature detection. It has not been compiled or run
natively.

`gens_pcm_render` accepts a caller-owned `GensPcmOutput`, disjoint left and
right planes, a frame count up to 882, and explicit options. Output is
always two samples per frame, left then right. Original mono results are
duplicated to both channels. The frame count is not the number of 16-bit
values. An empty block is allowed. Only samples below `output.frames * 2`
are current.

The options preserve distinct original paths:

- Playback scales each plane by the unsigned 16-bit master volume with
  32-bit wrapping multiplication and arithmetic shift by 8. The original
  default is 128, but the caller supplies the value explicitly.
- Scalar stereo clips to `[-32768, 32767]`. Scalar mono adds the planes
  with 32-bit wrapping, clips the sum, then halves it.
- Softening uses the original five-tap edge-clamped filter, wrapping
  32-bit arithmetic, and signed division toward zero. It disables the
  MMX conversion path, as in Gens.
- The MMX option reproduces sample semantics in portable C, not MMX
  instructions. Mono pairs clip each channel first, shift each right,
  then add. Odd-length blocks use the original special first-sample path,
  including its different negative clipping thresholds. Stereo's odd
  first sample clips negative values at `-32767`, not `-32768`.
- Dump mode ignores master volume, softening, and MMX selection. It
  preserves the mixer planes, matching the original dump functions. The
  original functions' trailing volume calculations do not change output.

Playback clears only the consumed prefix of both mixer planes after
conversion. Softened samples are calculated before either plane is
cleared. Dump mode does not consume any samples. Samples outside the
active prefix are not changed. The original slowdown/repeated-frame
DirectSound buffer policy is frontend behavior and is not included.

Output storage must be separate from both complete mixer arrays.
Null pointers, out-of-range counts, nonboolean options, and overlapping
active mixer planes or output storage fail before modifying the buffers
or result. The caller must report failure rather than exposing a stale
output block. Options and storage must remain guest-owned and follow
snapshot rules.

`gens_sound_output` converts the existing `Seg_L`, `Seg_R`, and `Seg_Length`
after the scheduler has flushed the chips. It does not execute or flush
chips itself. A future guest frame entry point must call it once per
completed frame in playback mode, including while fast-forwarding or
muted at the host. A `GetAudio` getter must only return the completed
block, not call this consuming function again. The stable output storage
and frame count have the shape needed by Chimera's stereo audio ABI.
The actual guest exports, machine entry point, and host delivery remain
unimplemented.

On the external Linux builder, `compile pcm-check` compares the original
i386 scalar and MMX routines with the candidate. The original routines
are extracted unchanged from the verified source. Sixteen trace digests
cover stereo/mono, softening, MMX selection, and dump mode across empty,
short, odd, even, and original frame-sized blocks and several master
volumes. The fixtures include extreme signed values, clipping boundaries,
and wrapping arithmetic. They compare output and both mixer planes.
A changed sample serves as a negative control.

The reference harness restores the host MMX state after calls, including
the original short-block path that omits `emms`. It skips empty dump calls
whose unused trailing calculations read uninitialized original locals.
Neither difference changes an emitted sample. This command has not been
run. PCM conversion does not resolve the sound-chip frame-end cursor,
the original Windows callback ABI, or movie synchronization.
