# 09 — Cache Strategy

## 1. The Unified Key

Everything cacheable is addressed by one scheme:

```
key = xxh3_128( node contentHash            // from the FrameGraph / asset
              , tick-quantized params        // for animated nodes: params sampled at tick
              , quality tier                 // full / half / quarter / proxy-input
              , pixel format & colorspace tag)
```

Because timeline nodes are immutable and hash-chained ([02 §4](02-system-architecture.md)),
**cache invalidation is not an event system** — an edit changes hashes, old
entries simply stop being requested and age out via LRU. This eliminates the
classic NLE bug family "stale frame after edit".

## 2. Cache Tiers

| Tier | Contents | Budget/eviction | Notes |
|---|---|---|---|
| **VRAM** | Working textures of recently used graph outputs & hot intermediates (e.g., keyed matte of a paused frame) | MemoryGovernor domain; LRU; drops first on budget-change | Hit = free composite reuse while tweaking one parameter |
| **RAM** | Decoded source frames (GOP granularity), audio chunks, waveform tiles, thumbnail bitmaps | LRU per domain | GOP-granular caching makes reverse playback & scrub-jitter cheap |
| **Disk (per-project cache dir)** | Render cache (composited segments), conform/optimized media, frame indexes, proxies, thumbnails, waveform pyramids | Size-capped, LRU across projects, fully regenerable | Location user-configurable (fast NVMe recommended); *never* inside the project file |

`cachedir/manifest.db` (SQLite) maps keys → files/offsets with generation
counters; the whole directory is disposable by contract ("Clear cache" is
always safe), and a version stamp nukes it wholesale on incompatible upgrades.

## 3. Render (Timeline) Cache

- Unit: **segment** (span between edit points) × quality tier. Stored as
  all-intra HEVC (hw-encoded) or LZ4-compressed 16F for effect-heavy short
  spans — encoder choice per segment by size/speed heuristic.
- Populated by: background render ([05 §6](05-playback-engine.md)) and
  opportunistic capture during normal playback (frames already computed are
  written out on the copy queue if the segment is marked hot).
- The timeline ruler shows cached state (Resolve-style colored bar) — users
  learn the system without reading docs.

## 4. Media Derivative Caches

Thumbnails (strip + bin poster), waveform pyramids ([07 §5](07-audio-engine.md)),
frame indexes ([04 §4](04-media-io-ffmpeg.md)), proxies ([04 §5](04-media-io-ffmpeg.md)) —
all keyed by **asset content hash**, so they survive file moves/renames and are
shared between projects. Generation jobs are idempotent and resumable.

## 5. What We Deliberately Don't Cache

- Final export output (no "smart render" reuse of previous exports in v1 —
  correctness risk > benefit; revisit with segment-hash matching in v2).
- Fully-composited *every-frame* preview (CapCut-style full bake). Segment
  granularity + fast graph re-exec is cheaper and stays correct under rapid
  editing.
