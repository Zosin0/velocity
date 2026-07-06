# 07 — Audio Engine

## 1. Real-Time Discipline (the iron rule)

The WASAPI render callback is real-time: **no locks, no allocations, no file
I/O, no logging, no shared_ptr copies** inside it. It does exactly one thing:
copy from a lock-free ring buffer to the device, report the device clock
position, and count underruns. Everything interesting happens on the
**AudioRender thread** that keeps that ring 150–500 ms full.

```
TimelineSnapshot ─► AudioGraph compile (per segment, mirrors video compiler)
     AudioRender thread: pull-model graph execution, float32 @ 48 kHz
        ClipSource(decode→resample→speed/pitch) 
          → clip gain/fade → clip FX chain
          → track sum → track FX chain → track fader/pan/mute/solo
          → master sum → master FX → limiter → meter taps
     ─► lock-free ring ─► WASAPI callback
```

## 2. Engine Facts

| Topic | Decision |
|---|---|
| Working format | float32, planar, 48 kHz fixed engine rate; block size = device period (typically 480 samples / 10 ms). Sources resampled via libswresample at the ClipSource node. |
| Channels | Stereo master v1; the graph is channel-count generic (mono/stereo clips pan into stereo); 5.1 is a format parameter later, not a rewrite. |
| Sample accuracy | Clip positions in ticks (1/48000 s) map 1:1 to samples — a deliberate consequence of the master tick choice ([02 §4](02-system-architecture.md)). Edits are sample-accurate; video-frame snapping is a UI behavior. |
| Sync | Audio device clock is playback master ([05 §2](05-playback-engine.md)). Drift between file A/V streams corrected using the frame index's audio priming data. |
| Scrub audio | Optional (default on): pitched mini-granules on scrub, like Resolve; implemented as a scrub-mode graph parameter. |
| Latency | Not a DAW: 30–60 ms output latency is fine and buys underrun immunity. Recording/VO (future) will need a low-latency input path — noted, not built. |

## 3. Automation & Keyframes

Volume/pan/FX parameters use the same `AnimatedValue` curves as video
(one animation system to test). The audio graph samples curves **per block
with linear ramping within the block** (avoids zipper noise). Fades = built-in
curve on the clip node (equal-power or linear, per-clip setting); crossfade =
paired fades created by one command.

## 4. DSP Node Set

- **v1:** gain, pan (constant-power), fade, parametric EQ (biquad cascade,
  high/low-pass included), compressor (soft-knee, lookahead), limiter
  (master default-on, true-peak), noise gate, normalize (peak & EBU R128
  loudness via libebur128 — analysis job writes a gain param, non-destructive).
- **v1.x:** pitch shift / time stretch (SoundTouch initially; RubberBand
  behind a licensing ADR), voice enhance preset (EQ+gate+comp macro), de-esser.
- **v2:** spectral noise reduction (FFT subtraction), plugin audio FX; VST3
  hosting is deliberately **out** until the plugin sandbox story exists
  ([12](12-plugin-architecture.md)) — VSTs are the #1 crash import vector in DAWs.
- All DSP is block-based float32 with SSE/AVX2 dispatch; kernels unit-tested
  against reference implementations with tone/impulse fixtures.

## 5. Waveforms

- Background job builds a **min/max/RMS pyramid** (like mipmaps: levels at
  1/256, 1/1024, 1/4096 samples-per-bin …) stored in the media cache, keyed by
  content hash. Timeline draws from the appropriate level for the zoom —
  waveform drawing cost is O(pixels), never O(samples).
- Generation streams the decode once, writes incrementally; the clip shows a
  progressive waveform while the job runs. Recording/speed-changed clips
  re-derive from the same pyramid (speed remaps bins; no regeneration).

## 6. Mixer UI Contract

The mixer panel is a pure view over track nodes: fader (dB, -inf…+6),
pan, mute/solo (solo-in-place semantics), meter (peak + RMS + clip hold,
fed by a lock-free meter tap at the track and master sum points, decimated to
30 Hz for the UI). Master shows LUFS-M/S/I when loudness metering is enabled.
