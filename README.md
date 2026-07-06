# Velocity — Native High-Performance Video Editor for Windows

> Working title "Velocity". A native C++20 / Direct3D 12 / FFmpeg non-linear video editor
> targeting content creators, with the ambition of CapCut usability on a
> DaVinci-Resolve-class engine architecture.

**Status: implementation Phase 0 complete (scaffold, portable toolchain, CI).
See [docs/PROGRESS.md](docs/PROGRESS.md) for the live session-by-session state.**

## Building

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup-devenv.ps1  # once: portable Windows SDK + FFmpeg into external/
. tools\devshell.ps1                                             # per shell: MSVC + SDK environment
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Requires: VS 2022 C++ toolset, CMake ≥ 3.28, Ninja. No admin rights and no
system Windows SDK needed (the SDK comes from NuGet into `external/`).

## Documentation Index

| Doc | Contents |
|-----|----------|
| [00 — Vision & Scope](docs/00-vision-and-scope.md) | PRD: goals, users, non-goals, MVP definition, competitive positioning |
| [01 — Technology Stack](docs/01-technology-stack.md) | Language, build, UI framework decision (Qt 6), graphics, libraries |
| [02 — System Architecture](docs/02-system-architecture.md) | Layering, module diagram, folder hierarchy, class model, data flow |
| [03 — Project Format](docs/03-project-format.md) | SQLite container spec, schema, autosave, crash recovery, versioning |
| [04 — Media I/O & FFmpeg](docs/04-media-io-ffmpeg.md) | FFmpeg abstraction layer, import, indexing, proxies, thumbnails |
| [05 — Playback Engine](docs/05-playback-engine.md) | Playback pipeline, A/V sync, seeking, frame accuracy, VFR handling |
| [06 — Render Engine & GPU](docs/06-render-engine-gpu.md) | Render graph, D3D12/RHI strategy, effects, color pipeline, chroma key |
| [07 — Audio Engine](docs/07-audio-engine.md) | WASAPI, mixing graph, effects chain, loudness, waveforms |
| [08 — Concurrency & Memory](docs/08-concurrency-and-memory.md) | Thread model, job system, memory pools, zero-copy strategy |
| [09 — Cache Strategy](docs/09-cache-strategy.md) | RAM/VRAM/disk cache tiers, invalidation, budgets |
| [10 — Export Pipeline](docs/10-export-pipeline.md) | Render queue, hardware encoding, smart rendering, formats |
| [11 — UI Architecture](docs/11-ui-architecture.md) | Layout, wireframes, docking, timeline widget, shortcuts |
| [12 — Plugin Architecture](docs/12-plugin-architecture.md) | C ABI, OpenFX, extension points, sandboxing roadmap |
| [13 — Quality: Standards, Testing, CI/CD](docs/13-quality-testing-ci.md) | Coding standards, test strategy, build & release pipeline |
| [14 — Roadmap, Milestones & Risks](docs/14-roadmap-risks.md) | Phased plan, complexity estimates, risk register, priorities |

## Ground Rules

1. **Performance is the product.** Every feature is designed against a frame budget.
2. **Non-destructive everywhere.** Media is immutable; the project is a description of operations.
3. **The engine does not know the UI exists.** `ui/` depends on `engine/`; never the reverse.
4. **No implementation before its design doc section is settled.**
