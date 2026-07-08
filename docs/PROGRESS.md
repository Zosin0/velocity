# PROGRESS — session continuity log

> Read this FIRST at the start of every session. Re-verify build + tests
> before writing new code. Update this file before ending every session.

## Current state (last verified: 2026-07-08, session 5)

- **Phase:** Phase 3 (functional MVP build-out) **IN PROGRESS** — core editing
  workflow is real end-to-end; see "Milestone status" below.
- **Build:** `dev` preset clean (MSVC 14.44, /W4 /WX, zero warnings).
- **Tests:** **65/65** pass via `ctest --preset dev`.
- **App:** launches, edits, plays with audio (including after edits — the
  play-after-edit freeze is fixed), saves/loads projects, exports verified
  MP4s that match the preview (layers/transforms/opacity composited).

### How to build (any session, this machine)
```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-devenv.ps1   # once, idempotent
. tools\devshell.ps1                                              # every shell
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## Milestone status ("complete a short video entirely inside Velocity")

| Capability | Status |
|---|---|
| Import video/audio/images incl. WEBP + SVG-rasterize (dialog, Explorer drop, bin drop) | ✅ real |
| Media bin: card view, async thumbnails, hover preview strip, facts line, search | ✅ real |
| Timeline: move/trim/split/delete, snapping+guides, cross-track drag, cursor zoom, v/h scroll | ✅ real |
| **Linked A/V clips**: split/move/trim/delete act on the pair; **Detach Audio** (menu/toolbar/Ctrl+Shift+D) | ✅ real, tested |
| Track management: add/remove tracks, per-track hide/mute/lock/gain, header toggles | ✅ real, tested |
| Image clips first-class (kind field, srcPts pinned to 0, free duration) | ✅ real, tested |
| Undo/redo incl. gesture coalescing (1 drag = 1 undo entry) | ✅ real |
| Playback: audio-master clock (IAudioClock), play/pause/stop/loop, frame step, wall-clock fallback | ✅ real |
| A/V sync: WASAPI ring + feeder thread; playhead slaved to device clock; restart-safe | ✅ real, tested |
| Stereo waveforms (per-channel peaks, background, cached) + draggable fade handles | ✅ real |
| Volume: per-clip gain/mute/fades, per-track faders in mixer, master fader, export master gain | ✅ real, tested |
| Inspector: position/scale/rotation/opacity/visibility + audio props | ✅ real |
| Multi-layer preview compositing in sequence space (== export geometry) | ✅ real (CPU/QPainter) |
| **Export compositing parity**: layers, transforms, opacity, alpha, gaps as black | ✅ real, pixel-tested |
| Export fast path (plain cuts → direct encode) ≈ session-3 speed | ✅ real, gate-tested |
| Project save/load (.velproj canonical JSON) incl. link groups + track flags | ✅ real, tested |
| Context menus (clip/track/background), SVG icon set, dark theme pass | ✅ real |
| Text/title clips | ❌ not started (design in gap 1) |

## Known gaps (be honest with the user about these)

1. **Text clips absent.** Decided design: title = clip with style fields
   (text/font/size/color) whose pixels are UI-rasterized (QPainter) to a
   versioned PNG under `%LOCALAPPDATA%/Velocity/titles/<clip>_<rev>.png`;
   engine treats it as an image asset (no Qt in engine). Editing restyles →
   new PNG revision (avoids decoder-cache staleness).
2. Preview color math is BT.601 (swscale default) for all content — docs/06
   calls for proper color management in the render graph; acceptable now.
3. No autosave/crash recovery yet (docs/03 journal) — project format is JSON
   interchange form; SQLite container (.vep) still to come.
4. Volume automation curves (keyframes) not built; architecture is the
   `AnimatedValue` design from docs/06 §4 — current envelope = clip gain +
   linear fades, which the mixer/export share.
5. Preview decode still happens synchronously on the UI thread (fast now via
   swscale, but the docs/05 prefetch/PlaybackController thread is the real
   design). 4K playback will need it plus the D3D12 render graph.
6. Editing while playing pauses playback (deliberate simplification, session
   4b). Resuming automatically after the edit is a possible polish item; the
   docs/02 design (engines adopt snapshots at frame boundaries) is the real
   fix and needs the playback-thread refactor of gap 5.
7. `velocity_unit_tests.exe` / `velocity.exe` occasionally need a SAC
   reputation warm-up on first run after rebuild (see machine constraints).
8. CI currently triggers on `pull_request` only (user's choice); pushes to a
   branch without a PR do not build.

## ⚠️ Machine constraints (discovered session 1) — READ BEFORE DEBUGGING

1. **Smart App Control ENFORCING**: blocks Debug-preset binaries (debug CRT).
   Use `dev` preset (RelWithDebInfo). A freshly built exe failing to start
   with an "Application Control policy" message is SAC, not your code —
   retry after a few seconds.
2. **No admin rights**: portable NuGet Windows SDK in `external/`, wired by
   `tools/devshell.ps1`. vcvarsall flows will NOT work.
3. Machine locale pt-BR (system error text in Portuguese).
4. RTX 5060 + Intel UHD 770. FFmpeg build has libopenh264 (deterministic
   fixtures + sw export fallback) and h264_nvenc/qsv/amf.
5. Qt 6.8.0 portable in `external/qt6`, deployed via windeployqt post-build.
   CI uses Qt 6.6.2 via install-qt-action; `cmake/qt.cmake` resolves
   windeployqt from whichever Qt is found (never hardcode that path again).

## Deviations from the architecture docs (deliberate, revisit later)

| Deviation | Reason | Revisit when |
|---|---|---|
| FetchContent instead of vcpkg manifest (docs/01) | no vcpkg; FFmpeg-from-source out of budget | CI hardening / binary release |
| FFmpeg = prebuilt BtbN n7.1 LGPL shared DLLs | same | same |
| Exceptions ON globally (docs/13 says off in engine) | spdlog bootstrap simplicity | engine hot paths |
| CPU compositing (QPainter preview / engine CPU compositor export), not D3D12 render graph (docs/06) | functional-completeness priority; both paths share geometry so the graph swaps in cleanly | render-graph phase |
| Project format = canonical JSON (docs/03 interchange), not SQLite container | fastest correct path to save/load; JSON form is docs-sanctioned | durability workstream (autosave/journal) |
| Playback audio path: feeder thread + SPSC ring feeding WASAPI | matches docs/07 real-time rule; simpler than full AudioGraph | audio engine build-out |
| Preview decode on UI thread (no PlaybackController thread yet) | simplicity; swscale made it fast enough for 1080p | playback-engine build-out (docs/05 §3) |

## Architectural decisions (cumulative)

- **One mixing implementation** (`engine::AudioMixer`) shared by export and
  playback — "what you hear is what you export" enforced structurally.
- **One sequential-read implementation** (`media::SequentialFrameReader`)
  shared by export and preview; it caches the last frame's full display
  interval (sub-frame requests never re-decode).
- **One compositing geometry**: aspect-fit into the sequence canvas, then
  pos/scale/rotation/opacity about the center — implemented twice (QPainter
  preview, `engine::compositeLayers` export) with identical math; the export
  fast path (single identity layer, same aspect, opaque format) bypasses it
  without changing output pixels.
- **Linked A/V clips** via `Clip::linkGroup`: session-level split/move/trim/
  delete operate on `engine::linkedMembers`; Detach keeps the group id as
  sync metadata (`linkDetached`). Persisted; group ids remapped on load.
- **Gesture transactions** on DocumentSession (`beginGesture`/`endGesture` +
  `UndoStack::replaceTop`) — interactive drags/sliders produce single undo
  entries, per docs/02 §5 transaction semantics.
- Timeline/Explorer/bin drags all carry file URLs → one drop handler.
- Hidden/zero-opacity clips and hidden/muted/locked tracks are resolver- and
  edit-level concepts (engine), not UI filters.

## Performance observations

- Preview conversion now goes through swscale (`media::RgbaConverter`,
  SIMD) — 1080p frame ≈ 2–4 ms vs tens of ms for the old per-pixel loops.
  Combined with the frame-interval cache, 60 Hz playhead over 30 fps media
  decodes each frame exactly once.
- Export of the 2 s gate fixture: ~0.25 s (fast path ≈ session-3 speed);
  composite path costs roughly 2–3× that on transform/multi-layer sections
  only. Canvas buffer reused across frames (no per-frame allocations).
- Waveform generation for a 3-min MP3 completes in ~1–2 s in background.
- Ring buffer 680 ms + 100 ms feed chunks: zero audible underruns.

## Next concrete unit of work (session 6)

1. **Text/title clips** per the design in gap 1 (model fields + title dialog +
   PNG rasterization service + timeline/inspector editing).
2. **Playback prefetch thread** (docs/05 §3): move decode off the UI thread,
   adopt snapshots at frame boundaries → un-pause-on-edit (gap 6), smoother
   scrubbing, groundwork for the quality ladder.
3. If budget allows: autosave timer writing `<project>.velproj.autosave`
   (gap 3), resume-after-edit polish.

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
  Commits: 8b1e138, 9e1c052, f62c99c, 1b42438, 2e6b405, b7e726f.
- **2026-07-07 (session 4, sprint):** Production-readiness sprint.
  **Engine:** linked A/V clips + Detach Audio (fixes "cut video, audio keeps
  playing on export"); track management (add/remove/hide/mute/lock/gain);
  ClipKind with image srcPts pinning; **CPU compositor for export parity**
  (aspect-fit + pos/scale/rotation/opacity, premultiplied bilinear blend) —
  export now renders exactly what the preview shows, incl. gaps as black;
  `Mp4Writer::writeRgbaFrame`; `media::RgbaConverter`; ExportSettings.
  masterGain; project JSON persists all new fields (link groups remapped on
  load). **UI:** timeline overhaul (track headers with toggle buttons,
  context menus, fade handles, snap guides, drop indicator, cursor-centered
  zoom, vertical scroll, rounded gradient clips, stereo waveforms); media
  bin card view (async thumbnails, ~2 s hover preview strip, facts line,
  empty state); mixer with per-track faders; SVG icon set (QSvgRenderer);
  toolbar/menus/transport rebuilt; preview composites in sequence space
  (== export geometry). **CI:** windeployqt resolved from the found Qt
  (root cause of the CI link failure), portable zip artifact (exe + Qt +
  FFmpeg DLLs), `--smoke` launch check. Commits: 55423a4, 2a3d441.
- **2026-07-07 (session 4b, user debug):** Fixed audio clock baseline capture
  (`clockStartSeconds_` captured after device `start()`); edits during
  playback now pause playback; cleaned Unicode corruption in mainwindow.cpp;
  play/pause button state detection. Commits: ed60c41, 797a4e6, merge b4a8f50.
- **2026-07-08 (session 5):** Bug-fix + performance session.
  **Play-after-edit freeze root-caused and fixed:** `AudioOutput::stop()`
  never `Reset()` the WASAPI client, so the next `start()` pre-filled a
  non-empty buffer → `AUDCLNT_E_BUFFER_TOO_LARGE` → silent failure + frozen
  IAudioClock → frozen playhead ("can't play after editing", because every
  edit now pauses). stop() Resets, start() pre-fills padding-aware, and the
  controller falls back to the wall clock whenever audio start fails —
  playback can no longer freeze. Restart regression test (3 start/stop
  cycles). **Preview speed:** frame conversion moved to swscale
  (`RgbaConverter`, thread-local cached context) replacing per-pixel loops;
  `SequentialFrameReader` caches the frame's display interval (fixes decoding
  + returning the next frame early on every 60 Hz UI tick). **Export speed:**
  single-layer/identity/same-aspect/opaque frames bypass RGBA+composite and
  feed the encoder directly (plain cuts back to ~0.25 s for the 2 s fixture);
  composite canvas reused across frames. 65/65 tests.
