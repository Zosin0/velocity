# 00 — Vision & Scope (PRD)

## 1. Product Vision

A native Windows video editor that opens in under two seconds, scrubs 4K source
material without stutter, and exports as fast as the encoder hardware allows.
It should feel like CapCut to a beginner and like a real NLE to a professional:
the simplicity is in the UI layer, not in a crippled engine.

## 2. Positioning

| Competitor | What we take | What we deliberately don't copy |
|---|---|---|
| CapCut | Instant usability, template-driven text/effects | Cloud lock-in, telemetry-heavy design |
| Clipchamp | Zero-friction import → export flow | Browser-grade performance ceiling |
| DaVinci Resolve | Engine architecture: render graph, caching, proxy workflow | Full color/Fusion/Fairlight page complexity (v1) |
| Premiere Pro | Keyboard-first editing model, trim tools | 20 years of legacy UI debt |
| VS Code | Command palette, keybinding system, extension model | — |

**The wedge:** every mainstream competitor is either Electron/browser-based
(Clipchamp; CapCut desktop is Chromium-based) or a 30-year-old codebase.
A disciplined native engine is a durable moat because it cannot be retrofitted.

## 3. Target Users (priority order)

1. **Short-form creators** (TikTok/Reels/Shorts) — fast cuts, captions, speed ramps, 9:16 presets.
2. **YouTube creators** — long timelines (30–120 min), multi-track, proxy workflow matters here.
3. **Streamers/podcasters** — audio quality tools, multi-hour source files, silence-cut workflows (future).
4. **Marketing/education teams** — templates, brand presets, batch export.
5. **Small production companies / pro editors** — adopt last; they validate the engine but must not drive v1 scope.

## 4. Core Philosophy (binding constraints)

- **Non-destructive:** the project file stores *operations over immutable media references*. No pixel of source media is ever modified.
- **Frame-accurate:** every edit, seek, and export lands on the exact frame. This is a correctness requirement, not a feature.
- **Responsive under load:** the UI thread never blocks on media I/O, decode, or GPU work. Ever. A dropped preview frame is acceptable; a frozen timeline is not.
- **Scale by degrading quality, not responsiveness:** under pressure the preview drops resolution/quality first, frame rate second, and never input latency.
- **Extensible without rewrites:** effects, transitions, codecs, importers, exporters are all plugin-shaped from day one, even while shipped built-in.

## 5. Scope Challenge (read this before the feature list)

The feature list in the original brief is roughly **Premiere Pro circa 2015 plus
CapCut's text engine** — that is 300+ engineer-years of software. Attempting it
as a monolithic v1 is the single largest project risk (see
[14 — Risks](14-roadmap-risks.md)). The architecture below supports all of it;
the **roadmap deliberately does not schedule all of it**. Specific pushbacks:

1. **"Unlimited" everything** — architected yes, but v1 ships with practical
   soft limits (e.g., 32 video tracks) so testing is tractable. Limits are
   config values, not structural.
2. **AVX-512** — cut from requirements. Consumer Intel silicon (Alder Lake+)
   fused it off; the video path is GPU-bound anyway. We ship SSE4.2 baseline +
   AVX2 dispatch. Revisit only if profiling shows a CPU hot loop that matters.
3. **CUDA as a first-class API** — rejected. D3D12 compute shaders run on all
   three vendors; NVENC/QSV/AMF are reached through FFmpeg. A CUDA path would
   double-maintain every kernel for one vendor. CUDA re-enters only if a
   specific AI model demands it (and then via ONNX Runtime / DirectML first).
4. **SVG import** — deferred. Real SVG animation/rendering is a browser-sized
   subproject. v1 rasterizes SVG at import (resvg/lunasvg) into a bitmap asset.
5. **FLV import** — deprecated container, near-zero creator demand in 2026.
   FFmpeg gives it to us nearly free, so it stays, but it gets zero dedicated
   test investment beyond the generic demux suite.
6. **Magnetic timeline AND ripple/roll/slip/slide** — these are two different
   editing paradigms (FCP X vs. Premiere). v1 ships the track-based model with
   magnet-style snapping. A true FCP-style magnetic timeline is a v2+ mode
   decision, not a checkbox.
7. **Color management** — absent from the brief, which is a trap. We fix the
   working color space contract in v1 (Rec.709/sRGB, range handling defined at
   decode) so HDR/wide-gamut can be added later without invalidating the
   architecture. Ignoring this is how editors end up with the permanent
   "exports look washed out" bug class.

## 6. MVP Definition

### v0.1 "Cutter" (internal milestone — proves the engine)
- Import MP4/MOV/MKV/MP3/WAV/PNG/JPG; media bin with thumbnails.
- Single sequence; 4 video + 4 audio tracks.
- Playback: real-time 1080p H.264/HEVC with hardware decode; JKL; frame step; audio-slaved A/V sync.
- Edits: trim, split, delete, ripple delete, move, snapping; unlimited undo/redo.
- Transform: position/scale/rotation/opacity (no keyframes yet).
- Audio: per-clip gain, fade in/out, track mute/solo, master meter.
- Export: MP4 H.264 (NVENC/QSV/AMF + x264 fallback) + AAC, three presets.
- Project save/load with autosave and crash recovery.

### v1.0 "Creator" (public MVP)
Everything in v0.1 plus:
- Keyframes on transform/opacity/volume with linear/bezier easing; curve editor.
- Text: styled titles (font/size/color/stroke/shadow/glow), Windows font enumeration, emoji; fade/slide animation presets.
- Effects: color correction (lift-gamma-gain, saturation, temperature/tint, exposure), LUT (.cube), Gaussian blur, sharpen, vignette, crop — all GPU.
- Transitions: cross dissolve, dip to black/white, wipe, slide, push; audio crossfade.
- Chroma key (green/blue/custom, spill suppression, feather, matte cleanup).
- Proxy generation + proxy playback toggle; background proxy jobs.
- Speed: constant rate change with pitch-corrected audio; reverse; freeze frame.
- Export: H.264/H.265, WEBM, GIF, audio-only, PNG/JPEG sequence; render queue; background export.
- 9:16 / 1:1 / 16:9 sequence presets; markers; in/out points.

### Explicit v1 non-goals (deferred, architecture-ready)
Nested timelines & compound clips (v1.x), bezier masking + tracking (v1.x),
speed ramps with optical-flow interpolation (v2), motion blur (v2), AI features
(v2, plugin-shaped), video scopes (v1.x), audio suite beyond EQ/compressor/
limiter/noise gate (v2), public plugin SDK (v2), magnetic timeline mode (v2+),
Google Fonts online browser (v1.x), AV1 encode (v1.x — hardware dependent),
ProRes export (v2 — licensing review required), batch/watch-folder export (v1.x).

## 7. Success Metrics

| Metric | Target |
|---|---|
| Cold start to interactive | < 1.5 s (warm OS cache < 800 ms) |
| Open a 1-hour 1080p project | < 3 s to interactive timeline |
| Timeline interaction latency | < 16 ms input-to-visual response, always |
| 4K H.264 single-stream playback | 60 fps sustained with HW decode, zero drops |
| 1080p timeline: 3 layers + color + text | Real-time preview on GTX 1660-class GPU |
| Export 1080p H.264 NVENC | ≥ 4× real-time on RTX 3060-class hardware |
| Peak RAM, 1080p project | < 2 GB excluding user-configured caches |
| Crash-free session rate | > 99.5 % (recovery restores to last committed operation) |

## 8. Minimum System Requirements (v1)

Windows 10 21H2+ / Windows 11, x64. D3D12 feature-level 11_0 GPU with 2 GB VRAM
(integrated Intel UHD 620 acceptable at reduced preview quality). 8 GB RAM.
DirectX 12 Agility SDK shipped with the app. WARP (software raster) preview is
**not** supported — users without a working GPU get a clear error. Decision
rationale: supporting WARP doubles the QA matrix for users who cannot run the
product acceptably anyway.
