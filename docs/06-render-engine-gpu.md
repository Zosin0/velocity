# 06 — Render Engine & GPU Strategy

## 1. FrameGraph Model

For each output frame, the `SequenceCompiler` produces a DAG:

```
SourceNode(clip A, pts, colorimetry) ─► ToWorking(color convert NV12→RGBA16F, range, matrix)
   ─► EffectNode(color correction) ─► EffectNode(chroma key) ─► TransformNode(pos/scale/rot, filtering)
SourceNode(title layer) ─► TextRasterNode ────────────────────────────┐
                                                                      ▼
                             TransitionNode(A×B, t) ─► CompositeNode(blend stack, premul)
                                                                      ▼
                                     OutputTransform(working→display or export space)
                                                                      ▼
                                              SinkNode(preview swapchain | export readback)
```

Properties:
- **Every node has a content hash**: `xxh3(node type, params at this tick, input hashes)`.
  The hash *is* the cache key at every tier (VRAM/RAM/disk). Identical
  sub-graphs across frames (still image + static effects) hit cache
  automatically — incremental rendering falls out of the design instead of
  being a feature.
- Compilation is cached **per timeline segment** (span between edit points):
  within a segment the graph shape is constant, only per-tick parameter values
  change; recompile happens on snapshot change for dirty segments only.
- The graph is quality-parameterized (resolution tier, effect quality level,
  proxy inputs) — see [05 §5](05-playback-engine.md).

## 2. Working Space & Pixel Format (the contract)

- Working format: **RGBA16F, premultiplied alpha, linear-light Rec.709**
  primaries for v1. Sources are converted at `ToWorking` (BT.601/709 matrix,
  full/studio range, transfer function per probe metadata). Output transform
  re-encodes for display (sRGB swapchain) or export (BT.709).
- Rationale: compositing/blending/scaling in gamma space is visibly wrong
  (dark fringes, wrong dissolves). 16F linear costs bandwidth but makes every
  effect correct by default, and it is the door through which HDR/wide-gamut
  walks later without re-architecting. 8-bit working pipelines are the
  unfixable legacy mistake of consumer editors — we refuse it now.
- Preview tier may use RGBA8 sRGB intermediates at quarter-res as the lowest
  quality rung; export always runs the full-precision path.

## 3. RHI / D3D12 Usage

| Concern | Approach |
|---|---|
| Queues | 1 graphics (composite/present), 1 compute (effects that overlap composite; color conversion of imported decode surfaces), 1 copy (uploads/readbacks). Export and preview share the device; export work rides a lower-priority context. |
| Synchronization | Timeline fences everywhere; one fence value per frame packet; no `WaitForGpu` stalls on hot paths. |
| Descriptors | Bindless-ish: one big shader-visible heap, indices passed in constants. Avoids per-draw descriptor churn for effect chains. |
| Resources | All frame-sized textures come from the **TexturePool** (keyed by size/format/usage); command allocators/lists pooled per frame-in-flight (triple buffered). |
| Residency | VRAM budget via `IDXGIAdapter3::QueryVideoMemoryInfo` + budget-change events; the VRAM cache is the eviction pressure valve ([09](09-cache-strategy.md)). |
| Shaders | Effects are HLSL compute where possible (simpler binding, async-queue friendly); graphics pipeline reserved for actual raster (text quads, transforms with sampling, final composite). Permutation cache persisted to disk. |
| Debugability | Debug layer + DRED breadcrumbs in dev builds; PIX markers around every graph node; the "kill device" test command ([05 §7](05-playback-engine.md)). |

## 4. Effects Framework

Every effect (built-in or plugin) implements the same contract ([12](12-plugin-architecture.md)):

```
describe() → params (typed, animatable, ranges, UI hints), 
             capabilities (GPU/CPU, temporal window, preview-quality variant)
prepare(format, quality) → pipeline/PSO selection
render(ctx { inputs, output, params-at-tick, scratch pool })
```

- Params are declarative → inspector UI, keyframing, serialization, and hashing
  are generic code; an effect author never writes UI.
- **Temporal effects** (motion blur v2, frame interpolation v2) declare an input
  window (`needs frames t−k…t+k`); the compiler widens prefetch accordingly.
  This capability flag exists in v1 even though no v1 effect uses it — it
  changes prefetch architecture and must not be retrofitted.
- CPU effects (rare; some audio-reactive/utility cases) receive pooled CPU
  frames; the graph inserts readback/upload nodes explicitly so their cost is
  visible in profiles rather than hidden.

### v1 effect set
Color correction (lift-gamma-gain, exposure, contrast, saturation, temp/tint,
hue), curves, levels, LUT (.cube, tetrahedral interpolation), Gaussian blur
(separable, radius-scaled by quality), sharpen (unsharp), vignette, crop,
transform (with high-quality Lanczos/Mitchell sampling), opacity/blend modes
(normal, add, multiply, screen, overlay, soft light), flip/mirror, pixelate.

### Chroma key (v1, flagship quality bar)
Compute pipeline: RGB→YCgCo distance vs. key color → soft matte (tolerance,
softness) → spill suppression (desaturate toward complement) → matte cleanup
(shrink/grow via min/max filter, feather via blur) → edge refinement pass.
Live parameter preview must run full-frame at interactive rates on the
integrated-GPU floor. Future AI segmentation slots in as an alternative
matte-source node (ONNX Runtime/DirectML), same node interface.

### Masks ([roadmap v1.x])
Mask = grayscale render target produced by a MaskNode (rect/ellipse/polygon/
bezier, feather, expansion, invert) consumed by any effect as an optional
input. Bezier rasterization via GPU fill (loop-blinn or coverage compute).
Tracking (v2) writes keyframes into the mask's transform — reuses the standard
animation system, no special data path.

## 5. Text Rendering

- DirectWrite: font enumeration, fallback chains, variable fonts, color emoji
  (COLR/SVG tables) → HarfBuzz shaping → glyph atlas (R8 SDF for scalable
  quality + color atlas for emoji) → instanced quad raster into a layer
  texture, composited like any source.
- Rich layout model (runs with per-run style: font/size/weight/fill/stroke/
  shadow/glow; paragraph alignment, line/letter spacing, boxes with vertical
  alignment) lives in `engine/text`; the inspector edits it via operations
  (same command pattern as timeline edits → text edits are undoable uniformly).
- Animated text presets = keyframe templates on per-glyph/per-line transform
  params (the layout exposes glyph clusters as animatable units — architecture
  in v1, full animator UI v1.x).

## 6. Transitions

A transition is an effect with two video inputs + progress `t` (already how the
graph models it). v1 set: cross dissolve (linear-light — this is why dissolves
look right), dip-to-color, wipe (directional, feathered), slide/push, zoom,
blur-through. Audio crossfade handled by the audio graph with equal-power
curves. Custom/plugin transitions get the same two-input contract.

## 7. Renderer Test Strategy (summary; details [13](13-quality-testing-ci.md))

Golden-image approval tests per node type and per composed graph, with
perceptual diff (FLIP) tolerances, run on WARP in CI for determinism +
on real GPUs nightly (NVIDIA/AMD/Intel runners) because WARP correctness ≠
driver correctness. (WARP is fine for *testing* even though unsupported for
users.)
