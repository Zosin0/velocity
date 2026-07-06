# 13 — Quality: Coding Standards, Testing, CI/CD

## 1. Coding Standards (enforced, not aspirational)

- **Style:** clang-format (repo file, CI-gated); naming: `PascalCase` types,
  `camelCase` functions/members, `snake_case` locals fine, `m_` members in
  stateful classes, `UPPER` only for macros (macros discouraged).
- **Language rules:** exceptions **off** in engine/media/gpu (Expected<T,E>
  results everywhere; exceptions allowed in app/ui where Qt lives anyway).
  RTTI off in hot libraries. No raw `new/delete` outside allocators. Ownership
  is explicit: `unique_ptr` default, `shared_ptr` only for genuinely shared
  immutable data (snapshots, frames) and stated in the type's docs.
- **Warnings:** `/W4 /permissive-` + selected `/w1XXXX` promotions, warnings-as-errors;
  clang-tidy profile (bugprone-*, performance-*, concurrency-*) gating CI.
- **Every PR:** review required, includes tests or a stated reason, updates
  the relevant design doc if it changes an architectural contract, no TODOs
  without a ticket id.
- **ADRs:** decisions that bind the future (new dependency, ABI change,
  lock-free structure, thread) require a one-page ADR in `docs/adr/`.

## 2. Test Strategy

| Layer | Tooling | What & gates |
|---|---|---|
| Unit | GoogleTest | model ops (trim/split/ripple invariants — property-based where cheap: random command sequences must preserve track invariants), time math (rational conversions, VFR maps — exhaustive over the ugly fps set: 23.976/29.97/59.94), compiler segment logic, cache keys, DSP kernels vs. reference |
| Media | GoogleTest + corpus | versioned **media corpus** (~100 files: every supported container/codec, VFR OBS captures, rotated phone video, broken-header MKV, truncated MP4, HDR-flagged, 10-bit, odd audio layouts) fetched by hash in CI; decode determinism, frame-index accuracy (decode frame N via index == decode frame N sequentially, for all N in sampled set) |
| Render approval | custom + FLIP perceptual diff | golden images per graph node & composed scenes; WARP in PR CI (deterministic), real GPUs nightly (NVIDIA/AMD/Intel runner pool) |
| Integration | scripted headless engine | project open→edit→export flows; **export correctness gates** ([10 §4](10-export-pipeline.md)): A/V sync, frame counts, color flags, loudness |
| Fuzzing | libFuzzer (clang-cl) | demuxer/probe/index inputs, project-file loader — continuous corpus, crash = P0 |
| Performance | Google Benchmark + benchmark projects | fixed benchmark suite (defined in [00 §7](00-vision-and-scope.md) metrics): startup, open-project, scrub latency, playback drops, export fps; nightly on fixed hardware; regression >5 % fails the build |
| Soak/stress | nightly | 4-hour scripted editing session under ASan (weekly TSan); memory growth beyond budget = failure |
| Manual | test plan per release | device matrix (iGPU-only laptop, NV/AMD/Intel dGPU, 4K/125 % DPI, multi-monitor), driver-update device-loss drill |

Rules: deterministic JobSystem mode for all functional tests
([08 §6](08-concurrency-and-memory.md)); flaky test = quarantined same day,
fixed or deleted within a week; UI tests kept thin (Qt widget logic is mostly
declarative; the expensive surface is engine behavior, tested headless).

## 3. CI/CD

**CI (GitHub Actions + self-hosted GPU/perf runners):**

```
PR:      format+tidy → build (MSVC Release+Debug, clang-cl) → unit+media(sub-corpus)
         → WARP render approvals → integration (headless) → package smoke
Nightly: full corpus · real-GPU approvals (3 vendors) · perf suite · ASan soak
         · fuzz continuation · installer build + signed artifact
Weekly:  TSan soak · dependency audit (vcpkg baseline bump PR, auto-tested)
```

- Build cache: vcpkg binary caching + ccache/sccache; PR build target < 10 min.
- Every merge to main produces an installable, versioned build (MSIX +
  traditional signed installer via WiX; winget manifest at release).
- **Release channels:** `dev` (every merge, internal) → `beta` (weekly,
  opt-in updater) → `stable` (monthly-ish, staged rollout 5 %→100 % gated on
  Crashpad crash-rate). Auto-updater delta-patches; the app itself decides
  restart timing (never mid-edit).
- Symbols archived per release; Crashpad → symbolicated dashboards; crash-rate
  gate ([00 §7](00-vision-and-scope.md)) is a hard promotion blocker.
- Telemetry: **opt-in**, counts + timings only (feature usage, perf
  distributions), no media metadata ever; a public doc lists every field.
