# 05 — Playback Engine

## 1. Responsibilities

Turn (TimelineSnapshot, transport state) into presented frames and audible
audio with: audio-master A/V sync, frame-accurate seek, JKL shuttle (−32×…32×),
scrub, loop, in/out ranges, dropped-frame accounting, and dynamic quality.

## 2. Clock Architecture — audio is master

```
WASAPI device clock (sample position, drift-corrected)
        │
        ▼
 MasterClock (ticks)  ──►  video scheduler: present frame whose interval covers now
        │
        └──► UI playhead (interpolated for smooth drawing)
```

- During playback the **audio device clock is the master**; video slaves to it.
  Human perception tolerates a dropped video frame far better than an audio
  glitch or drift.
- With no audible audio (muted, stills-only), a steady `QueryPerformanceCounter`
  clock substitutes seamlessly (same interface).
- Scrub/pause uses a stepped clock (explicit tick set by UI); the same
  pipeline serves both — scrubbing is just "seek repeatedly, fast".

## 3. Pipeline & Threads

```
                       (UI thread)  transport commands, snapshot publishes
                              │ lock-free mailbox
                              ▼
                    ┌───── PlaybackController thread ─────┐
                    │ adopts snapshots at frame boundary   │
                    │ SequenceCompiler (cached per segment)│
                    │ schedules prefetch window            │
                    └───┬───────────────────────────┬──────┘
        video jobs      │                           │  audio pull
                        ▼                           ▼
              JobSystem decode workers      AudioRender thread (feeds ring buffer)
                        │                           │
                        ▼                           ▼
              GPU submit thread ── fences ──► WASAPI callback (real-time, lock-free)
                        │
                        ▼
              Waitable swapchain present (VSync-aligned)
```

- **Prefetch window:** video ≈ 4–8 frames ahead (adaptive to measured decode
  latency); audio ≈ 150–500 ms ahead into a lock-free ring. Reverse playback
  prefetches whole GOPs and serves them backwards from cache.
- **Frame delivery contract:** GPU submit publishes `(tick, texture, fence)`;
  the present step waits on the fence *on the GPU timeline*, never the CPU.
- Every stage is instrumented (Tracy zones + a rolling stats block: decode ms,
  graph-exec ms, queue depths, drop count) surfaced in a dev HUD and the
  dropped-frames indicator.

## 4. Seeking & Frame Accuracy

- Seek(tick): compiler maps tick → per-clip source pts via the frame index
  ([04 §4](04-media-io-ffmpeg.md)) → DecodeService `readFrameAt` (keyframe +
  roll-forward) → graph executes for exactly that tick → present. Target
  budget: < 80 ms for a cold 4K H.264 seek, < 16 ms warm.
- **Seek storms** (user drags playhead): coalescing mailbox keeps only the
  newest target; in-flight decodes for stale targets are cancelled via
  cooperative cancellation tokens on every job.
- Still frames while paused always render at **full quality** (a paused
  degraded frame reads as "the app is broken").

## 5. Dynamic Quality Ladder

When the frame budget is missed (rolling p95 over the last second), degrade in
order; restore in reverse when headroom returns:

1. Preview resolution ½ → ¼ (compile parameter; caches keyed separately)
2. Expensive effects to preview-quality variants (each effect declares one — e.g., smaller blur kernels)
3. Effects bypass for flagged "heavy" effects (user-visible badge: "preview quality reduced")
4. Frame-rate halving (present every other frame; audio never degrades)

The ladder is policy in one place (`playback/QualityGovernor`), not scattered
if-statements. User can pin quality (Full/Half/Quarter) like Resolve.

## 6. Background Render (v1.x)

Idle time → render dirty timeline segments at preview quality into the
timeline cache ([09](09-cache-strategy.md)) so complex sections play back
pre-composited. Uses the same compiler/graph with a low-priority GPU queue and
strict "yield to interactive" rules. Architected now (cache keys + segment
dirty-tracking exist in v1), scheduled after MVP.

## 7. Failure & Degradation Behavior

- Decoder starvation → hold last frame, show subtle spinner after 150 ms, log
  dropped-frame stats; never block the UI thread.
- Device lost (GPU reset/driver update) → RHI raises DeviceLost → engine tears
  down GPU resources, re-creates device, invalidates VRAM caches, resumes at
  current tick. Tested with a debug "kill device" command from day one —
  retrofit-testing device-loss is misery.
- Audio device change/unplug → WASAPI event → seamless re-open on new default
  device; ring buffer refilled; sub-100 ms gap acceptable.
