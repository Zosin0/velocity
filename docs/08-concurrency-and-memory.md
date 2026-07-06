# 08 — Concurrency & Memory Strategy

## 1. Thread Inventory

| Thread(s) | Owns | Talks via |
|---|---|---|
| UI (Qt main) | Widgets, DocumentSession, command execution, snapshot publishing | queued Qt signals in; lock-free mailboxes out |
| PlaybackController | Transport state, compiler invocation, prefetch scheduling | mailbox in; job submissions out |
| JobSystem pool (TBB, N = cores−2, min 2) | Decode, index, thumbnails, waveforms, proxy gen, file I/O, background render | job handles + cancellation tokens |
| GPU submit | Command-list recording/submission, swapchain present | SPSC queue of frame packets; fences |
| AudioRender | Audio graph execution, ring-buffer feeding | ring buffer; parameter snapshots |
| WASAPI callback (OS) | copy ring→device | lock-free ring only |
| ExportController (per active export) | export loop, encoder feeding | job submissions; progress events |
| Housekeeping | autosave journal flush, cache eviction, telemetry | timers + mailboxes |

Rules:
- **Priorities matter more than counts:** interactive decode jobs preempt
  background jobs (two-tier job queues: `Interactive`, `Background`; background
  workers also OS-priority-lowered). Proxy generation must never make scrubbing
  stutter.
- **Every job is cancellable** (token checked at packet/row granularity) and
  **tagged** (Tracy + watchdog: a job overrunning 100 ms on the interactive
  queue is a logged bug).
- Cross-thread communication is *data*, not shared mutable objects: snapshots
  (immutable), frame packets (refcounted, immutable payload), commands/events
  (POD-ish messages). `std::mutex` is allowed only in cold paths (project I/O,
  cache directory maintenance) and its use requires a comment justifying it.

## 2. Frame Data Lifetime (the memory hot path)

`VideoFrame`/`AudioChunk` are refcounted handles over pooled payloads:

```
TexturePool (VRAM)     — buckets by (w,h,format,usage); LRU within budget
FramePool (RAM)        — 64B-aligned slabs for CPU frames, size-class buckets
ChunkPool (RAM, audio) — fixed block-size ring-friendly buffers
```

- Release returns to pool; pools trim on budget pressure events. Steady-state
  playback performs **zero** frame-sized heap allocations — verified by a CI
  perf test asserting allocator stats over a 30 s playback of the benchmark
  project.
- Zero-copy chain: HW decoder surface (D3D11) → shared-handle import →
  compute color-convert into pooled working texture → composite → present.
  CPU touches pixels only for: software-decode fallback, CPU-only effects,
  export readback (and readback uses persistently mapped staging ring buffers).
- `TimelineSnapshot` nodes come from a dedicated slab pool with a freelist —
  edits allocate dozens of small nodes; pooling keeps commit latency flat and
  fights fragmentation from structural sharing.
- Undo pressure valve: snapshots beyond the memory budget serialize their
  command records and drop node references (redo replays commands — slightly
  slower, bounded memory).

## 3. Budget Manager

Central `MemoryGovernor` with per-domain budgets (defaults; user-configurable):

| Domain | Default budget |
|---|---|
| VRAM frame/texture pool + cache | 60 % of adapter budget (dynamic via DXGI events) |
| RAM decoded-frame cache | 25 % of physical RAM, cap 8 GB |
| RAM waveform/thumbnail | 256 MB |
| Disk cache (proxies, render cache, indexes) | 20 GB default, user-set, LRU by project |

Domains register eviction callbacks; the governor issues pressure levels
(green/yellow/red). Red = caches shrink hard + preview quality ladder engages.
OS memory-pressure (`QueryMemoryResourceNotification`) and DXGI budget-change
events feed it.

## 4. SIMD Policy

Runtime dispatch (SSE4.2 baseline / AVX2) via function-pointer tables set at
startup. Candidate loops only: waveform min/max reduction, audio mix/meters,
CPU pixel conversions in the software fallback, hash functions. Everything
frame-sized belongs on the GPU; a request to hand-vectorize a new CPU loop
requires a profile first. No AVX-512 (see [00 §5](00-vision-and-scope.md)).

## 5. Coroutines & Async Shape

C++20 coroutines are used at the **orchestration** layer (import pipeline,
export loop, multi-step services: `Task<T>` awaiting jobs/fences/I/O) where
they replace callback chains — not in per-frame hot loops. The `Task<T>`
runtime schedules continuations onto the JobSystem; frame-hot paths stay as
explicit state machines (measurable, no allocation surprises).

## 6. Determinism & Testing Hooks

- The JobSystem supports a **single-threaded deterministic mode** (seeded,
  run-to-completion) used by unit/approval tests — concurrency bugs shouldn't
  make render tests flaky.
- TSan (clang-cl) and ASan builds run the integration suite in CI weekly;
  the lock-free structures (SPSC/MPSC queues, seqlock snapshot slot, audio
  ring) are additionally model-checked with stress tests + verified against
  a relacy-style harness at introduction, then frozen (novel lock-free code
  requires an ADR — the default answer is "use the existing primitives").
