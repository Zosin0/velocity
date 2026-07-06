# 01 — Technology Stack

Decisions below are binding until overturned by a written ADR (architecture
decision record) in `docs/adr/`.

## 1. Language & Toolchain

| Area | Decision | Rationale |
|---|---|---|
| Language | **C++20** (MSVC primary, clang-cl kept green in CI) | Coroutines for async pipelines, `std::span`, concepts for the RHI/plugin templates. C++23 features adopted individually once MSVC support is stable. |
| Compiler | MSVC 19.4x + clang-cl 18+ | Two compilers keep the code honest and enable clang tooling (clang-tidy, ASan on Windows). |
| Build | **CMake ≥ 3.28 + Ninja + CMakePresets.json** | Industry default; presets give reproducible dev/CI configs. |
| Dependencies | **vcpkg (manifest mode, pinned baseline)** | Binary caching in CI; overlay ports for our FFmpeg build flags. |
| Rust? | Considered, rejected for core | FFmpeg/D3D12/Qt interop is all C/C++; a bilingual core taxes a small team. Isolated leaf tools (e.g., a future media indexer service) may use it later. |

## 2. UI Framework — the big decision

### Requirement recap
Dockable professional layout, custom high-performance timeline, embedded D3D12
swapchain preview, rich text editing, dark theme, keyboard-first, accessibility,
IME/emoji/Unicode, and no Electron.

### Candidates

**Qt 6 (chosen)**
- ✅ Mature docking (Qt Advanced Docking System — used by real NLEs and DAWs), styling engine for the dark theme, full IME/Unicode/accessibility/screen-reader support, font enumeration, rich text, SVG icons, internationalization.
- ✅ Proven in this exact domain: DaVinci Resolve, Nuke, Maya, OBS ecosystem tools ship on Qt.
- ✅ `QWindow`-level integration lets us own a raw HWND for the D3D12 preview swapchain and for the custom timeline surface.
- ⚠️ LGPLv3 obligations: **dynamic linking only**, ship license notices, allow library replacement. Acceptable for a commercial desktop app; commercial license is a later option if we need static linking.
- ⚠️ Qt Quick/QML is **not** used for v1. QML's scene graph and its threading model fight our D3D12 engine for the GPU and add a JS runtime. We use **Qt Widgets** for chrome + **custom-rendered surfaces** for timeline and preview.

**WinUI 3 (rejected)**
- Modern Windows look for free, but: C++/WinRT ergonomics are poor, docking must be built from scratch, the framework is still churning, tooling for complex custom-drawn controls is weak, and it forecloses any future non-Windows port. High risk, low differentiated value — our UI is 90 % custom surfaces anyway.

**Dear ImGui + custom widgets (rejected for product UI)**
- Superb for debug/dev tooling (we DO embed it behind a dev flag for engine introspection panels), but production gaps are disqualifying: accessibility/screen readers, IME composition, RTL text, OS-native text editing behaviors, tooltips/menus that respect OS conventions. Rebuilding those is years of work that Qt already did.

### UI performance rules
1. The **timeline** is one custom widget rendered by our own engine (D3D11-on-12 or Direct2D on the shared device) — not thousands of Qt child widgets. Qt handles input/focus/accessibility bridging; we handle pixels.
2. The **preview** is a raw `IDXGISwapChain4` on an HWND we own inside the layout, flip-model, tearing-allowed for scrub, presented by the render engine — Qt never touches those pixels.
3. All Qt model/view data (media bin, inspector) is fed from engine snapshots via queued signals; no engine locks are ever held on the UI thread.

## 3. Graphics

| Area | Decision |
|---|---|
| API | **Direct3D 12** behind a thin internal RHI (render hardware interface) |
| Why not D3D11 | We need async compute queues (decode-composite overlap), explicit residency/budget control for VRAM caching, and D3D12 video-decode interop. The RHI keeps the door open for Vulkan (Linux port) without committing to it now. |
| RHI scope discipline | The RHI wraps **only what we use**: device, queues, command lists, textures/buffers, descriptors, compute+graphics pipelines, timeline fences, video-decode interop. It is not a general-purpose engine abstraction; scope creep here is a named risk. |
| Shaders | HLSL 6.x via DXC, compiled offline in the build; a shader-cache with runtime permutation compile for effect graphs. |
| Interop | Hardware decode surfaces (D3D11 textures from DXVA/FFmpeg d3d11va) imported via shared handles / `ID3D11On12Device` — zero-copy NV12 → compute shader color conversion. Migrate to native `d3d12va` decode as FFmpeg support matures. |
| Presentation | DXGI flip model, waitable swapchain, independent flip; HDR10 output is a later ADR but the swapchain code assumes format is configurable. |

## 4. Media

| Area | Decision |
|---|---|
| Demux/decode/encode | **FFmpeg 7.x libraries** (libavformat/avcodec/avfilter/swscale/swresample/avutil), linked as DLLs, wrapped by our `media/` abstraction (see [04](04-media-io-ffmpeg.md)). Never shell out to ffmpeg.exe. |
| Build | Custom vcpkg overlay port: enable dxva2/d3d11va/d3d12va, nvenc/nvdec headers, qsv (libvpl), amf; disable everything we don't ship to cut binary size and patent surface. |
| Licensing | LGPL build (no `--enable-gpl`): means **no libx264/x265** in the shipped binary. Hardware encoders cover most users; software fallback is **OpenH264 (BSD, Cisco-licensed)** and SVT-AV1. A GPL-clean stance is a business decision to make explicitly — flagged in [14 — Risks](14-roadmap-risks.md). |
| Codec licensing (H.264/HEVC patent pools) | Must be resolved before commercial release; hardware-encoder path shifts much of it to the GPU vendor, but legal review is a roadmap gate. |
| Images | Windows Imaging Component (WIC) for PNG/JPEG/TIFF/BMP; libwebp for WebP; resvg for SVG rasterization. |

## 5. Audio

| Area | Decision |
|---|---|
| Device I/O | **WASAPI** shared mode (event-driven) default; exclusive mode optional. No dependency on higher-level frameworks. |
| DSP | Own mixing graph in float32; libswresample for resampling/channel layout; **libebur128** for LUFS; RubberBand (GPL — see licensing note) *or* SoundTouch (LGPL) for time-stretch — decision ADR-0007 after quality tests; start with SoundTouch to stay GPL-clean. |
| Format | Internal: float32 planar, 48 kHz working rate, resample at import boundaries. |

## 6. Infrastructure Libraries

| Concern | Choice | Notes |
|---|---|---|
| Logging | **spdlog** | Ring-buffer sink for crash reports; per-subsystem loggers; compile-time level stripping in release. |
| JSON | **nlohmann/json** for config/interchange; **not** on hot paths. Project persistence is SQLite ([03](03-project-format.md)). |
| Testing | **GoogleTest + GoogleMock**; **Catch2 rejected** to standardize on one. Approval tests for render output (image diff with perceptual tolerance). |
| Benchmarks | Google Benchmark, tracked in CI with regression gates. |
| Profiling | **Tracy** (frame + zone + GPU + lock instrumentation compiled into internal builds); ETW/WPA for OS-level investigation; PIX for GPU captures. |
| Tasking | **Intel oneTBB** for the job system core (work stealing, flow graph unused), wrapped by our `JobSystem` facade so it is replaceable. Custom lock-free SPSC/MPSC queues for the real-time audio and playback paths where TBB is inappropriate. |
| Memory | mimalloc as global allocator; custom pool/arena allocators for frames, timeline nodes, and undo records ([08](08-concurrency-and-memory.md)). |
| Crash reporting | Crashpad (Sentry-compatible) with minidumps + spdlog ring buffer attachment. |
| Text shaping | HarfBuzz + DirectWrite font enumeration/fallback; FreeType rasterization into glyph atlases (color emoji via DirectWrite/COLR path). |
| SQLite | Project container + caches metadata. WAL mode. |
| UUID/hash | xxHash3 for content hashes; UUIDv7 for entity ids (time-ordered → better SQLite locality). |

## 7. Explicitly Rejected

| Technology | Reason |
|---|---|
| Electron / WebView2 UI | Violates the core premise. |
| .NET/C# host with C++ engine | GC pauses adjacent to real-time paths, two-runtime debugging, marshalling tax on the timeline model. |
| Vulkan-first | Slower bring-up on Windows, worse video-decode interop story, no D3D-only debugging ecosystem (PIX). Kept open via RHI. |
| OpenGL | Legacy; poor multithreading; dead end on Windows. |
| Boost (broad) | Only if a single header solves a real problem; no framework-level adoption. |
| Custom serialization format | SQLite + defined schema beats hand-rolled binary for durability, tooling, and forward compatibility. |
