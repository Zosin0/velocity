# 10 — Export Pipeline

## 1. Shape

Export is **the playback pipeline without a clock**, driven as fast as
resources allow — same `SequenceCompiler`, same FrameGraph, full quality tier,
plus encoder sinks. This identity is a core correctness property: *what you
previewed is what you export* (modulo preview quality), because it is
literally the same code.

```
ExportController (coroutine loop)
  for tick in range:                     # N ticks in flight (pipelined, default 4)
    graph = compile(snapshot, tick, FullQuality)
    gpuFrame = execute(graph)                       # low-priority GPU context
    hwEncode: pass GPU surface directly (NVENC/QSV/AMF via FFmpeg hwframes, zero readback)
    swEncode: async readback via staging ring → pooled CPU frame → encoder thread
  audio: AudioGraph offline render (faster than real-time, block pipeline)
  Muxer: interleave, faststart, metadata, chapter/marker export
```

- Video and audio render **concurrently**; the muxer's interleaving window
  provides backpressure so neither side balloons memory.
- Ordering guarantee: encoder input strictly monotonic; the in-flight window
  is a reorder buffer keyed by tick.
- Cancellation: token checked per frame; partial file cleanup on cancel;
  pause/resume supported (encoder state permitting — else restart-from-segment).

## 2. Render Queue & Background Export

- Queue of jobs (sequence, range, preset, destination); serial execution by
  default (GPU contention makes parallel exports usually slower — measured
  before we allow a parallel toggle).
- **Export while editing:** the export holds its own snapshot (immutability
  makes this trivially safe) and rides the low-priority GPU context + Background
  job tier. Editing responsiveness wins by policy; expected export slowdown
  while actively editing ~30–50 %.
- Progress: per-job fps, ETA (rolling-window), preview thumbnail every N
  frames; failures carry the full typed error + log slice for support.

## 3. Formats & Presets (v1)

| Container | Video codecs | Audio | Notes |
|---|---|---|---|
| MP4 | H.264, H.265 (hw: NVENC/QSV/AMF; sw fallback OpenH264 / SVT-HEVC-less: see note) | AAC | faststart on; primary path |
| MOV | H.264, H.265 | AAC/PCM | ProRes is v2 pending licensing review |
| MKV / WEBM | H.264/H.265 / VP9+SVT-AV1 | AAC/Opus | AV1 encode v1.x (hw where available) |
| GIF | palette-gen two-pass | — | short-range guard (warn > 30 s) |
| PNG/JPEG sequence | — | — | plus audio-only WAV/MP3/AAC/FLAC export |

Licensing note ([01 §4](01-technology-stack.md)): the LGPL FFmpeg stance means
no x264/x265. If quality benchmarking shows hw+OpenH264 insufficient for the
software-fallback population, licensing x264 commercially is the documented
escape hatch (business decision, tracked in risks).

Presets: curated ladder (YouTube 4K/1080p, Shorts/Reels/TikTok 9:16, Podcast
audio, Archive) + custom presets stored as JSON, importable/shareable. UI
exposes quality 1–100 + advanced drawer; per-encoder parameter mapping is our
curated table, not raw FFmpeg options.

## 4. Correctness Gates (CI-enforced)

- **A/V sync:** exported file's first/last audio sample and video frame
  timestamps verified against timeline ground truth for every preset (test
  media with clapper patterns; automated offset detection ≤ ±10 ms).
- **Duration & frame count:** exact expected frame count per fps/range combo,
  including odd rationals (29.97, 23.976) — off-by-one-frame exports are a
  release blocker.
- **Color:** exported BT.709 flags verified (ffprobe) + decoded-pixel golden
  check; the "washed out export" class gets a permanent regression suite.
- **Loudness preset check:** presets with loudness targets verify integrated
  LUFS on output.
