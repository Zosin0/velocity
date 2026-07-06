# PROGRESS — session continuity log

> Read this FIRST at the start of every session. Re-verify build + tests
> before writing new code. Update this file before ending every session.

## Current state (last verified: 2026-07-06, session 1)

- **Phase:** Phase 0 (scaffold) **COMPLETE**. Next: Phase 1 risk spikes.
- **Build:** `release` and `dev` presets build clean (MSVC 14.44, /W4 /WX).
- **Tests:** 9/9 pass via `ctest --preset release` (rational-time math + FFmpeg integration smoke).
- **App:** `build/release/bin/velocity.exe` launches, logs FFmpeg runtime versions, exits 0.

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

**Phase 1, Spike A:** FFmpeg H.264 decode of a real MP4 → CPU frame with
correct pts (uses `media/` + rational time), then hardware `d3d11va` decode
path → D3D11 texture. Acceptance test: decode frame N by index == decode frame
N sequentially (pts equality) on a generated test asset (generate tiny test
MP4s with the bundled `external/ffmpeg/bin/ffmpeg.exe` into `build/testmedia/`
— do NOT commit media binaries).

Then Spike B (Win32 window + DXGI flip-model swapchain — note: Qt decision
pending, spike uses raw Win32 deliberately), then Spike C (WASAPI clock).

## Session log

- **2026-07-06 (session 1):** Toolchain bootstrapped from empty machine
  (no SDK/CMake/Ninja present). Phase 0 complete: CMake+presets, portable SDK,
  FFmpeg imported targets, foundation lib (rational time + logging), media lib
  (FFmpeg smoke), velocity.exe launch/exit, 9 unit tests, GitHub Actions CI,
  this file. Discovered + worked around Smart App Control (see constraints).
