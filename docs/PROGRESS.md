# PROGRESS — session continuity log

> Read this FIRST at the start of every session. Re-verify build + tests
> before writing new code. Update this file before ending every session.

## Current state (last verified: 2026-07-07, session 3)

- **Phase:** Phase 3 (functional MVP build-out) **IN PROGRESS** — core editing
  workflow is real end-to-end; see "Milestone status" below.
- **Build:** `dev` preset clean (MSVC 14.44, /W4 /WX, zero warnings).
- **Tests:** **45/45** pass via `ctest --preset dev`.
- **App:** launches, edits, plays with audio, saves/loads projects, exports
  verified MP4s. Smoke-tested after every unit this session.

### How to build (any session, this machine)
```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-devenv.ps1   # once, idempotent
. tools\devshell.ps1                                              # every shell
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## Milestone status ("complete a short video entirely inside Velocity")

| Capability | Status |
|---|---|
| Import video/audio/images (dialog, Explorer drop, bin drop) | ✅ real |
| Media bin: search, metadata tooltips, drag-to-timeline | ✅ real |
| Timeline: move/trim/split/delete, snapping, cross-track drag, zoom/scroll | ✅ real |
| Undo/redo incl. gesture coalescing (1 drag = 1 undo entry) | ✅ real |
| Playback: audio-master clock (IAudioClock), play/pause/stop/loop, frame step | ✅ real |
| A/V sync: WASAPI ring + feeder thread; playhead slaved to device clock | ✅ real |
| Real waveforms (background generation, cached) + fade indicators | ✅ real |
| Per-clip volume/mute/fade in/out; master fader; real peak meters | ✅ real |
| Inspector: position/scale/rotation/opacity/visibility + audio props | ✅ real (see gap 1) |
| Multi-layer preview compositing with transforms | ✅ real (CPU/QPainter) |
| Project save/load (.velproj canonical JSON), dirty flag, missing-media warning | ✅ real |
| Export MP4 H.264+AAC (NVENC→openh264 fallback), progress/cancel/verify | ✅ real, gate-tested |
| Text/title clips | ❌ not started (next session, design below) |
| Export compositing parity (layers + transforms in export) | ❌ gap 1, next session |

## Known gaps (be honest with the user about these)

1. **Export renders only the top video layer, full-frame.** Transforms and
   multi-layer stacks show in the preview but are NOT applied on export yet.
   Next unit: CPU compositor in `engine/` (RGBA canvas, sws per layer,
   pos/scale/opacity + bilinear rotation), used by exportSequence via
   `resolveVideoLayersAt`, golden-image tested. Then preview and export share
   semantics until the D3D12 render graph (docs/06) replaces both internals.
2. **Text clips absent.** Decided design: title = clip with style fields
   (text/font/size/color) whose pixels are UI-rasterized (QPainter) to a
   versioned PNG under `%LOCALAPPDATA%/Velocity/titles/<clip>_<rev>.png`;
   engine treats it as an image asset (no Qt in engine). Editing restyles →
   new PNG revision (avoids decoder-cache staleness).
3. Image clips re-seek the single-frame decoder every playhead move (works,
   wasteful). Fix when the compositor lands: clamp image srcPts to 0.
4. Preview color math is BT.601-ish for all content (docs/06 calls for
   proper color management in the render graph — acceptable for preview now).
5. No autosave/crash recovery yet (docs/03 journal) — project format is JSON
   interchange form; SQLite container (.vep) still to come.
6. `velocity_unit_tests.exe` occasionally needs a SAC reputation warm-up on
   first run after rebuild (see machine constraints).

## ⚠️ Machine constraints (discovered session 1) — READ BEFORE DEBUGGING

1. **Smart App Control ENFORCING**: blocks Debug-preset binaries (debug CRT).
   Use `dev` preset (RelWithDebInfo). A freshly built exe failing to start
   with an "Application Control policy" message is SAC, not your code.
2. **No admin rights**: portable NuGet Windows SDK in `external/`, wired by
   `tools/devshell.ps1`. vcvarsall flows will NOT work.
3. Machine locale pt-BR (system error text in Portuguese).
4. RTX 5060 + Intel UHD 770. FFmpeg build has libopenh264 (deterministic
   fixtures + sw export fallback) and h264_nvenc/qsv/amf.
5. Qt 6.8.0 portable in `external/qt6`, deployed via windeployqt post-build.

## Deviations from the architecture docs (deliberate, revisit later)

| Deviation | Reason | Revisit when |
|---|---|---|
| FetchContent instead of vcpkg manifest (docs/01) | no vcpkg; FFmpeg-from-source out of budget | CI hardening / binary release |
| FFmpeg = prebuilt BtbN n7.1 LGPL shared DLLs | same | same |
| Exceptions ON globally (docs/13 says off in engine) | spdlog bootstrap simplicity | engine hot paths |
| CPU (QPainter) preview compositing, not D3D12 render graph (docs/06) | functional-completeness priority; D3D12 graph is a dedicated workstream | render-graph phase |
| Project format = canonical JSON (docs/03 interchange), not SQLite container | fastest correct path to save/load; JSON form is docs-sanctioned | durability workstream (autosave/journal) |
| Playback audio path: feeder thread + SPSC ring feeding WASAPI | matches docs/07 real-time rule; simpler than full AudioGraph | audio engine build-out |

## Architectural decisions this session

- **One mixing implementation** (`engine::AudioMixer`) shared by export and
  playback — "what you hear is what you export" enforced structurally.
- **One sequential-read implementation** (`media::SequentialFrameReader`)
  shared by export and preview.
- **Gesture transactions** on DocumentSession (`beginGesture`/`endGesture` +
  `UndoStack::replaceTop`) — interactive drags/sliders produce single undo
  entries, per docs/02 §5 transaction semantics.
- Timeline/Explorer/bin drags all carry file URLs → one drop handler.
- Hidden or zero-opacity clips fall through to lower tracks in resolvers.

## Performance observations

- Playback at 1080p H.264 with hw decode: preview conversion (per-pixel YUV→
  RGB in C++) is the hot spot; fine at 30 fps content on this machine but the
  planned render graph / at least SIMD or sws conversion will be needed for
  4K. Waveform generation for a 3-min MP3 completes in ~1–2 s in background.
- Export of the 2 s gate fixture: ~0.2–0.3 s with NVENC (≫ real-time).
- Ring buffer 680 ms + 100 ms feed chunks: zero audible underruns in testing.

## Next concrete unit of work (session 4)

1. **Engine CPU compositor** for export parity (gap 1) + tests comparing a
   composited export frame against the resolver's layer stack (golden pixels).
2. **Text/title clips** per the design in gap 2 (model fields + title dialog +
   PNG rasterization service + timeline/inspector editing).
3. If budget allows: image srcPts clamp (gap 3), autosave timer writing
   `<project>.velproj.autosave`.

## Session log

- **2026-07-06 (session 1):** Toolchain bootstrap from empty machine; Phase 0
  scaffold (CMake/presets/portable SDK/FFmpeg targets/foundation/CI); Phase 1
  spikes A/B/C (decoder + D3D12 + WASAPI) — 21 tests. SAC constraint found.
- **2026-07-06 (session 2):** Phase 2: timeline model + resolvers (29 tests);
  Qt 6.8 UI shell (docking, timeline widget, preview, bin, inspector, mixer).
  Note: session ended with mp4_writer/audio_decoder written but NOT in the
  build, and export/save/open as fake dialogs — repaired in session 3.
- **2026-07-07 (session 3):** Functional-MVP push. Fixed corrupted src/main
  (build was broken). Real export pipeline + dialog with doc-10 gates as
  tests (frame count exact, A/V streams, duration, cancel). Real audio
  playback: AudioMixer + SPSC ring + WASAPI, playhead on IAudioClock; Stop/
  Loop/Space transport; real mixer meters + master fader. Clip model gained
  gain/mute/fades/transform/hidden with updateClip/moveClipToTrack edits;
  inspector fully wired; preview composites all layers with transforms.
  Real waveforms (background peaks, cached); native drag & drop everywhere;
  magnet snapping; cross-track drags; bin search + metadata. Project
  save/load (.velproj JSON) with dirty tracking and missing-media handling.
  Preview now renders RGBA/RGB24/YUVJ420 sources (images). 45/45 tests.
  Commits: 8b1e138, 9e1c052, f62c99c, 1b42438, 2e6b405, b7e726f + this one.
