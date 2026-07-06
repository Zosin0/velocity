# 04 — Media I/O & FFmpeg Integration

## 1. Abstraction Contract

FFmpeg types (`AVFormatContext`, `AVCodecContext`, `AVFrame`, …) never appear
outside `src/media/`. The rest of the codebase sees:

```cpp
// media/probe
MediaInfo probe(const Path&);            // streams, codecs, duration, fps (incl. VFR flag),
                                         // color primaries/transfer/matrix/range, rotation,
                                         // channel layout, sample rate, HDR metadata

// media/decode — one reader per (asset, stream) usage site
class VideoDecoder {
    // Seek+decode such that the frame containing `pts` (stream timebase) is returned.
    // Guarantees frame accuracy via index (see §4). Non-blocking variants for prefetch.
    Expected<VideoFrame> readFrameAt(StreamPts pts, DecodeHints);
    Expected<VideoFrame> readNext();     // sequential fast path used by playback/export
};
class AudioDecoder { Expected<AudioChunk> readAt(SamplePos, size_t frames); };

// VideoFrame: refcounted; payload is EITHER a GpuSurfaceRef (hw decode, NV12/P010
// in a D3D texture) OR a CpuFrameRef (pooled buffer). Carries pts, duration,
// colorimetry, and the asset content-hash for cache keying.
```

Design rules:

- **Decoders are single-threaded objects owned by the DecodeService**; parallelism
  comes from having many decoder instances, not from sharing one.
- All calls return `Expected<T, MediaError>` — corrupt files are a normal
  input, not an exception path.
- `DecodeHints` carries: direction (fwd/backwards-scrub), quality (full/half —
  enables h.264/hevc `lowres`-style or proxy redirect), and hw/sw preference.

## 2. DecodeService (the decoder pool)

- Owns N decoder slots (default: `min(video tracks visible + 2, hw session limit)`).
  NVDEC sessions are a scarce resource (driver-dependent limits); the service
  tracks vendor session budgets and falls back to software decode gracefully.
- **Decoder reuse by locality:** a request at pts *p* prefers a live decoder
  whose position is in `[p - 0.5s, p)` (sequential win) before opening/seeking.
  Scrubbing backwards uses a GOP-stepping strategy (seek to previous keyframe,
  decode forward, cache the whole GOP — see [09](09-cache-strategy.md)).
- Hardware path: FFmpeg `d3d11va` hwaccel with **our** `AVHWDeviceContext`
  created on the engine's adapter → frames arrive as D3D11 NV12 textures →
  imported into D3D12 via `ID3D11On12` / shared handles → color-convert compute
  shader. Zero CPU-side copies for the entire playback path.
- Software path: pooled CPU buffers, `sws_scale` only when the GPU path can't
  take the pixel format directly (rare; we upload native formats and convert
  in shader).

## 3. Import Pipeline

```
Drop/dialog → Importer front (extension+sniff) 
  → probe (fast, <50ms budget, header-only)
  → asset row committed (UI shows item immediately, "pending" badge)
  → background jobs, priority-ordered:
       1. thumbnail strip (poster + N sparse frames)
       2. audio waveform pyramid
       3. frame index build (§4) for formats needing it
       4. proxy generation (policy-driven, §5)
```

Images go through WIC/libwebp/resvg into an `ImageAsset` (decoded lazily,
cached as GPU texture with mip chain). EXIF rotation and embedded ICC handled
at import; alpha is premultiplied at upload (documented engine-wide convention).

## 4. Frame Index — the frame-accuracy backbone

For every video stream we build a compact sidecar index (in the cache dir):
`(pts, dts, file_offset, keyframe flag, size)` per packet, plus derived GOP
table. Built during the first background pass (demux-only, no decode ≈ I/O
speed).

What it buys:
- **Exact seek:** binary-search pts → previous keyframe → decode forward exact
  count. No "seek then guess" drift, no FFmpeg `AVSEEK_FLAG_BACKWARD` surprises.
- **VFR truth:** the index *is* the pts map. The timeline maps ticks → source
  pts through it; we never assume constant frame duration. (VFR screen
  recordings — OBS captures — are a top-3 support-ticket generator for every
  editor; this is our answer, decided up front.)
- **Cheap duration/fps validation:** container-header lies (common in MKV/FLV)
  are corrected after indexing; the asset's probe row is updated.
- Long-GOP audio (per-packet priming/trailing) offsets recorded for
  sample-accurate audio seek.

## 5. Proxy & Optimized Media

- Policy default: auto-generate proxies when `(codec is long-GOP AND (height > 1080
  OR fps > 60)) OR decode-cost probe fails real-time`, user-overridable per
  asset/project.
- Proxy format: **half or quarter resolution, all-intra hardware-encoded HEVC**
  (falls back to NV12-friendly MJPEG if no encoder). All-intra = every frame is
  a keyframe = perfect scrubbing. Audio is not proxied.
- Proxies live in the project cache dir keyed by content hash → survive
  project moves, shared across projects referencing the same file.
- Toggle is a `DecodeHints` redirect: the engine's cache keys include
  proxy/full flag so caches never cross-contaminate.

## 6. Encoding (export-side, summarized here, pipeline in [10](10-export-pipeline.md))

- `media/encode` wraps encoder + muxer: `VideoEncoder` (consumes `VideoFrame`,
  hw path feeds NVENC/QSV/AMF via FFmpeg hwframes without readback when the
  frame is already on GPU), `AudioEncoder`, `Muxer` (interleaving, faststart).
- Encoder selection: capability probe at startup (cached) → ranked list per
  codec (NVENC > QSV > AMF > software) with per-encoder quality parameter
  mapping tables — "quality 1–100" in the UI maps to sane CRF/CQ/bitrate per
  encoder, curated by us, not exposed FFmpeg soup.

## 7. Error-Handling & Robustness Policy

- Every FFmpeg call site goes through checked wrappers that translate
  `AVERROR` into typed `MediaError` + context (file, stream, op).
- A corrupt packet mid-file → decoder resyncs to next keyframe, playback shows
  a slate for the gap, an event is logged for the diagnostics panel; the app
  never crashes on bad media (fuzzing target in CI, see [13](13-quality-testing-ci.md)).
- FFmpeg version upgrades: `media/` has its own test suite against the media
  corpus; upgrading FFmpeg is a routine PR gated by that suite, not an event.
