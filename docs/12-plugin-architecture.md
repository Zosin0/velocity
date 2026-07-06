# 12 — Plugin Architecture

## 1. Strategy: internal-first, ABI-stable, publish later

All built-in effects/transitions already implement the plugin contract
([06 §4](06-render-engine-gpu.md)) — we are our own first plugin author, so the
API is battle-tested before any third party sees it. Public SDK ships v2.

## 2. Extension Points

| Point | v1 (internal) | Public |
|---|---|---|
| Video effects / transitions | ✅ | v2 |
| Audio effects | ✅ | v2 |
| Title/animation templates (data-only: JSON + assets, no code) | ✅ | **v1.x** — safe, high-value, community templates like CapCut |
| Import/export format handlers | interface exists | v2+ |
| AI tools (matte source, audio enhance, transcript) | node interfaces reserved | v2 |
| Automation/scripting | — | v2+ (deliberate: a scripting surface is a support surface) |

## 3. ABI Design (the part you can't change later)

- **Pure C ABI**, versioned structs with size fields (`OfxPropertySuite`-style
  capability negotiation): `velocity_plugin_api.h` is the single source of
  truth, compiles as C99, no STL/exceptions across the boundary.
- Host provides suites: parameters (declare/get-at-tick), images (request
  input/output, formats), GPU (device handles, shader-module registration),
  memory (pooled scratch), logging.
- GPU effects submit **HLSL/DXIL through the host's pipeline system** —
  plugins never own a device; this keeps scheduling, budgets, and device-loss
  handling centralized.
- **OpenFX evaluated and deferred:** OFX brings an ecosystem (Sapphire, etc.)
  but its GPU story (OpenCL/CUDA-era suites) fits our D3D12 graph poorly, and
  hosting arbitrary OFX binaries in-process is a stability tax. Decision: our
  native ABI first; an OFX *bridge plugin* is a contained v2+ project if
  demand proves out. (ADR-0009.)

## 4. Stability & Isolation Roadmap

- v1 internal: in-process, but every plugin call wrapped with SEH crash
  attribution (crash reports name the plugin) + timing watchdog.
- v2 public: in-process but load-quarantined (a plugin that crashed last
  session starts disabled), signed-manifest metadata, version gating.
- v2+: out-of-process execution for CPU effects & importers (shared-memory
  frames); GPU effects likely stay in-process with attribution — full GPU
  isolation costs more perf than the ecosystem justifies early.

## 5. Discovery & Packaging

`%ProgramData%/Velocity/plugins/<vendor>.<name>/` containing `manifest.json`
(id, version, min-host-version, entry DLL, capabilities) + payload. Templates
(data-only) are `.veltpl` zip bundles installable by drag-and-drop.
