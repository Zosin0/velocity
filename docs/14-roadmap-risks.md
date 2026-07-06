# 14 — Roadmap, Milestones & Risk Register

## 1. Team Assumption (estimates are meaningless without it)

Baseline plan assumes **5 senior engineers**: engine/graphics, media/codecs,
audio+infra, UI ×2 — plus a designer and part-time QA from Phase 4. With a
solo developer, multiply calendar by ~4–5× and cut v1 scope to the v0.1
"Cutter" + minimal effects; the architecture holds either way.

## 2. Phases

The brief's 10 phases are re-sequenced into vertical slices — **a thin
end-to-end path exists from Phase 2 onward** (import→timeline→play→export),
because pipeline integration risk must burn down first, not last. "Rendering
after Timeline, Audio after Rendering" as strict layers would discover the
hard sync/interop bugs a year too late.

| Phase | Contents | Duration | Complexity | Exit criteria |
|---|---|---|---|---|
| **1. Foundations** | repo/CMake/vcpkg/CI skeleton, foundation libs (jobs, memory, log, time), RHI core + triangle-to-swapchain, FFmpeg probe/decode spike incl. **hw-decode→D3D12 zero-copy proof**, WASAPI spike, Qt shell with docking | 6–8 wk | High (interop spikes) | The three scary interops (decode→GPU, Qt↔swapchain, audio clock) each demoed |
| **2. Walking Skeleton** | model+snapshots+commands, minimal compiler/graph (source→transform→composite), single-clip playback w/ audio sync, trim/split/move on 2 tracks, MP4 H.264 export, SQLite save/load | 8–10 wk | Very High | "Cut two clips together, hear audio, export, reopen project" — all pipelines exist |
| **3. Editing Depth** | full edit toolset (ripple/roll/slip/slide, multi-select, snapping, markers, in/out), undo hardening, media bin, thumbnails/waveforms, frame index, offline media, autosave/recovery | 8–10 wk | High | v0.1 "Cutter" milestone ships internally; 5k-clip timeline holds 60 fps |
| **4. Engine Depth** | quality ladder, caches (VRAM/RAM/disk + render cache), proxy pipeline, seek-storm/JKL/scrub polish, device-loss recovery, export queue + background export, perf suite live | 8–10 wk | Very High | Metrics table ([00 §7](00-vision-and-scope.md)) green on reference hardware |
| **5. Creative Toolset** | keyframe system + curve editor, effect framework + v1 effect set, transitions, chroma key, text engine v1, audio DSP set, mixer | 10–14 wk | Very High | Feature-complete v1.0 "Creator" scope |
| **6. Hardening & Beta** | corpus expansion, fuzz fixes, soak, DPI/multi-monitor/driver matrix, onboarding/templates/presets, installer+updater+crash pipeline, closed beta cycles | 8–12 wk | Medium-High | Crash-free ≥ 99.5 %, beta NPS signal, export correctness gates green |
| **→ v1.0 public** | | **~11–14 months** | | |
| 7. v1.x train | masks+tracking, nested sequences, scopes, background render, AV1, template marketplace-lite, Google Fonts, batch export | quarterly | — | — |
| 8. v2 themes | plugin SDK public, AI features (matte/transcript/enhance via ONNX+DirectML), speed ramps + optical flow, motion blur, magnetic mode study, ProRes, VST3 decision | — | — | — |

Priority rule when time pressure hits (it will): **engine metrics > edit feel >
effect count**. A fast, stable cutter with 12 great effects beats a slow app
with 60 mediocre ones — CapCut's actual lesson.

## 3. Risk Register

| # | Risk | L×I | Mitigation |
|---|---|---|---|
| R1 | **Scope explosion** (the brief is 300+ engineer-years) | H×H | MVP contract in [00 §6](00-vision-and-scope.md); every feature request routes to v1.x/v2 buckets; monthly scope review vs. calendar |
| R2 | **HW decode→D3D12 interop instability across drivers** (the classic native-NLE tarpit) | H×H | Phase-1 spike on all 3 vendors; SW-decode fallback always present; nightly real-GPU CI; device-loss drill from day 1 |
| R3 | A/V sync & frame-accuracy edge cases (VFR, odd rationals, long-GOP audio priming) | H×H | Rational time everywhere, frame index as ground truth, export correctness gates in CI, ugly-fps unit matrix |
| R4 | Codec/patent licensing (H.264/HEVC pools; GPL contamination) | M×H | LGPL-clean build, hw-encoder-first, legal review as explicit pre-release gate, x264 commercial license as priced fallback |
| R5 | Qt LGPL compliance / future licensing cost | L×M | Dynamic linking policy enforced in build; commercial license budgeted as option |
| R6 | Performance erosion over time (death by a thousand features) | H×M | Nightly perf suite with hard regression gates; frame-budget culture; Tracy in every dev build |
| R7 | Key-person risk (5 people, 5 deep specialties) | M×H | These design docs stay current (PR-gated); ADR discipline; pairing on the interop layers |
| R8 | Competitor speed (CapCut ships weekly) | M×M | Don't compete on feature count; compete on performance + trust (offline, no cloud lock-in) — positioning, not roadmap churn |
| R9 | Beta reveals the timeline "feel" is off (subjective, unmeasurable) | M×H | Trim-to-preview + input-latency budgets from Phase 2; editor-in-residence testing from Phase 3, not Phase 6 |
| R10 | SQLite project format meets a need for merge/collab later | L×M | Command journal is already an op log — CRDT/sync layers on top are feasible; JSON export keeps data open |

## 4. First Five Concrete Engineering Tasks (post-approval)

1. Repo bootstrap: CMake presets, vcpkg manifest, CI skeleton, clang-format/tidy gates.
2. Spike A: FFmpeg d3d11va decode → D3D12 texture → composite → swapchain, on NVIDIA+AMD+Intel.
3. Spike B: Qt 6 shell + ADS docking + embedded swapchain HWND resize/DPI behavior.
4. Spike C: WASAPI ring-buffer playback of a decoded AAC stream with drift measurement.
5. `foundation/`: JobSystem facade + FramePool + rational time types, with the deterministic test mode.
