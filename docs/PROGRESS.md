# PROGRESS — session continuity log

> Read this FIRST at the start of every session. Re-verify build + tests
> before writing new code. Update this file before ending every session.

## Current state (last verified: 2026-07-06, session 1)

- **Phase:** Phase 1 (risk spikes) **COMPLETE**. Next: Phase 2 thin end-to-end slice.
- **Build:** `dev` preset builds clean (MSVC 14.44, /W4 /WX).
- **Tests:** 21/21 pass via `ctest --preset dev`:
  - rational time (exact NTSC math), FFmpeg integration smoke
  - Spike A: probe + VideoDecoder — sequential frame counts exact, pts monotonic,
    `readFrameAt` == sequential scan, D3D11VA hw decode pts == sw (ran on RTX 5060)
  - Spike B: D3D12 device (hw + WARP fallback), clear→readback pixel-exact,
    flip-model swapchain presents on a real Win32 window
  - Spike C: WASAPI shared-mode output plays a tone, IAudioClock advances at
    wall rate (±10 %), ≤2 underruns — viable playback master clock
- **App:** `velocity.exe` launches, logs FFmpeg runtime versions, exits 0.
- **Spike A zero-copy status:** hw decode produces D3D11 textures; the
  D3D11→D3D12 shared-handle import into the renderer is NOT wired yet (allowed
  fallback per execution prompt). Current path for hw frames is transfer-to-CPU.
  Wire the zero-copy (or measured copy) path in Phase 2 when the render graph
  consumes decoded frames.

### How to build (any session, this machine)
```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-devenv.ps1   # once, idempotent
. tools\devshell.ps1                                              # every shell
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## ⚠️ Machine constraints discovered (session 1) — READ BEFORE DEBUGGING "weird" failures

1. **Smart App Control is ON and ENFORCING** (`VerifiedAndReputablePolicyState=1`).
   It **blocks Debug-preset binaries** (debug CRT linkage fails its reputation
   heuristics) with CodeIntegrity events 3033/3077: "Application Control policy
   blocked this file". **Release/RelWithDebInfo binaries pass.** Therefore:
   - Work in the `dev` preset (RelWithDebInfo). Do not use the `debug` preset
     on this machine; it exists for CI/other machines.
   - Every new binary hash is a fresh cloud-reputation lottery. If a freshly
     built exe suddenly fails to start with the block message, it is SAC, not
     your code. Only the user can disable SAC (Windows Security → App & browser
     control; irreversible). Flagged to the user in session 1.
2. **No admin rights assumed.** Windows SDK is the **portable NuGet SDK** in
   `external/` (10.0.26100.8249), wired by `tools/devshell.ps1`. No system SDK
   exists — `vcvarsall`-based flows will NOT work on this machine.
3. Machine locale is pt-BR: system error text appears in Portuguese.
4. Installed this session via winget: CMake 4.3.4, Ninja 1.13.2 (user-wide).

## Deviations from the architecture docs (deliberate, revisit later)

| Deviation | Reason | Revisit when |
|---|---|---|
| FetchContent instead of vcpkg manifest (docs/01) | no vcpkg on machine; FFmpeg-from-source out of session budget | CI hardening / before any binary release |
| FFmpeg = prebuilt BtbN n7.1 LGPL shared DLLs in `external/` | same | same |
| Exceptions ON globally (/EHsc) incl. engine libs (docs/13 says off in engine) | spdlog bootstrap simplicity; no engine hot paths exist yet | when `engine/` module lands |
| `.gitignore` excludes `external/` — toolchain deps re-fetched per clone | they are 600+ MB of third-party binaries | fine permanently |
| No Qt yet | UI phase not started; decision on aqtinstall-per-user vs. descope to Win32 shell due when Phase 1 spike B starts | Phase 1 spike B |

## Scope status vs. the "80% core" execution prompt

- Phase 0 scaffold — **DONE** (this session).
- Phase 1 spikes (decode→GPU, swapchain window, A/V clock) — not started.
- Phase 2 thin slice — not started.
- Phase 3 build-out — not started.
- Deferred-by-prompt features: unchanged, none touched.

## Next concrete unit of work

**Phase 2 — thin end-to-end slice**, in these units (each: build+test before next):
1. `engine/model`: TimelineSnapshot (immutable, shared_ptr nodes), Sequence/
   Track/Clip with tick math; commands: add clip, split clip; undo stack.
   Pure-CPU unit tests (no media needed).
2. `engine/compile`: snapshot + tick → "what source frame shows at this tick"
   (single video track + single audio track resolution). Unit tests.
3. Export slice: model+compiler+decoder+encoder → MP4 H.264 (hw encoder w/
   openh264 fallback) + AAC audio; **doc-10 gates as tests**: exact frame
   count, duration, A/V both present. (Export before playback: it's testable
   headless and exercises the same pipeline.)
4. Playback slice: Win32 window + swapchain + decoder + WASAPI, audio-master
   clock, play/pause/seek on a cuts-only timeline. Manual-run target
   (`velocity.exe <file>`) + automated clock-sync test where possible.
5. Wire hw-decode surfaces into the preview path (zero-copy D3D11→D3D12
   import, or measured-copy fallback — document whichever ships).

## Session log

- **2026-07-06 (session 1):** Toolchain bootstrapped from empty machine
  (no SDK/CMake/Ninja present). Phase 0 complete: CMake+presets, portable SDK,
  FFmpeg imported targets, foundation lib (rational time + logging), media lib
  (FFmpeg smoke), velocity.exe launch/exit, 9 unit tests, GitHub Actions CI,
  this file. Discovered + worked around Smart App Control (see constraints).
- **2026-07-06 (session 1, cont.):** Phase 1 complete — Spike A (probe +
  frame-accurate decoder, sw + D3D11VA), Spike B (D3D12 device/readback/
  swapchain + Win32 window), Spike C (WASAPI output + master-clock property).
  21/21 tests. Machine facts: RTX 5060 + Intel UHD 770; FFmpeg build has
  libopenh264 (deterministic h264 test fixtures + software export fallback)
  and h264_nvenc/qsv/amf for hardware export.
