# 11 — UI Architecture

## 1. Principles

Dark, quiet, keyboard-first. Chrome recedes; media and timeline dominate.
Every mouse action has a keyboard path; every command is in the palette
(Ctrl+Shift+P — the VS Code affordance, absent from every mainstream NLE and
cheap for us because commands are already first-class objects).

## 2. Main Layout (default workspace)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ☰  Project ▾   Edit  View            ⏺ autosaved   ▍Export ▐  ─ □ ✕          │ toolbar
├───────────────┬─────────────────────────────────┬────────────────────────────┤
│ MEDIA         │                                 │ INSPECTOR                  │
│ ┌───┐ ┌───┐   │                                 │ ┌ Transform ────────────┐  │
│ │▓▓▓│ │▓▓▓│   │          PREVIEW                │ │ Position  x 0   y 0  ◆│  │
│ └───┘ └───┘   │        (D3D12 swapchain)        │ │ Scale     100%       ◆│  │
│ ┌───┐ ┌───┐   │                                 │ │ Rotation  0°         ◆│  │
│ │▓▓▓│ │▓▓▓│   │                                 │ │ Opacity   100%       ◆│  │
│ └───┘ └───┘   │  ◀◀  ◀ ▶  ▶▶   00:01:24:13     │ ├ Effects ──────────────┤  │
│ ───────────   │        ├────────●────────┤      │ │ + Color  + Key  + Blur│  │
│ ⚡Effects      │                                 │ └───────────────────────┘  │
│ ⇄ Transitions │                                 │ (Audio Mixer tab)          │
│ T Text        │                                 │                            │
├───────────────┴─────────────────────────────────┴────────────────────────────┤
│ ⏱ 00:00   00:10      00:20      00:30      00:40      00:50      01:00      │
│ V3 ┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈  🔒 👁                                 │
│ V2 ┈┈┈┈▐████ TITLE ████▌┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈                       │
│ V1 ▐██████ clip_A ██████▌▞▞▐████████ clip_B ████████▌┈┈┈┈┈                    │
│ A1 ▐▁▂▄▆▄▂▁▂▄▆▄▂▁▂▄▆▄▂▁▌  ▐▁▂▄▆▇▆▄▂▁▂▄▆▄▂▁▂▄▆▄▂▁▂▄▌┈┈┈                       │
│ A2 ┈┈┈▐▂▃▂▃▂▃▂ music ▂▃▂▃▂▃▂▃▂▃▂▃▂▃▂▃▂▃▂▃▂▃▂▌┈┈┈┈┈┈┈                          │
├──────────────────────────────────────────────────────────────────────────────┤
│ ▶ 29.97fps  1920×1080  ⬤ cache 64%  GPU 41%  0 dropped   snapping ⌁ magnet   │ status
└──────────────────────────────────────────────────────────────────────────────┘
```

- Left sidebar tabs: Media / Effects / Transitions / Text / Assets.
- Inspector tabs: Properties / Effects / Audio Mixer.
- All panels dock/float/tab via Qt Advanced Docking System; workspaces
  (Edit / Audio / Color / Export) are saved layouts; user layouts persist.

## 3. The Timeline Widget (the hard part)

One custom widget, engine-rendered ([01 §2](01-technology-stack.md)):

- **Virtualized:** draws only visible clips from an interval tree over the
  snapshot; a 5,000-clip project draws what's on screen, full stop.
- Layered draw model: ruler / tracks / clips (body, trim handles, fades,
  waveform tiles, thumbnail strips, labels) / overlays (playhead, markers,
  in-out, snap guides, selection band, drag ghosts) — each layer dirty-tracked
  so a moving playhead redraws one bar, not the world.
- Interaction is a **tool-state machine** (Select/Blade/Hand/Trim variants:
  ripple, roll, slip, slide) consuming semantic hit-tests (`clip body`,
  `clip edge`, `fade handle`, `keyframe dot`). Every gesture emits preview
  transactions ([02 §5](02-system-architecture.md)) so the preview window live-updates
  during trims (trim-to-preview is a flagship feel feature).
- Snapping: candidate set (edit points, playhead, markers, in/out) in a sorted
  structure; magnet radius in screen px; toggleable per-gesture with Alt held.
- Zoom: Ctrl+wheel around cursor; zoom-to-fit; per-track height presets;
  60 fps pan/zoom is a hard perf gate (test-enforced on the 5k-clip project).
- Audio waveforms/thumbnail strips arrive async from caches — placeholder
  shimmer, never a blocking fetch on the draw path.

## 4. Preview Panel

Raw swapchain HWND ([01 §2](01-technology-stack.md)). Overlays (transform
gizmo, crop handles, mask outlines, safe areas, title bounding boxes) are
drawn by the engine into the same present pass — no Qt overpaint, no z-fight
between toolkit and swapchain. Transport bar, quality selector
(Auto/Full/Half/Quarter), grid/safe-area toggles are Qt widgets around it.

## 5. Shortcuts & Commands

- `KeymapService`: every user action is a named `CommandId` with default
  bindings; user overrides in `keymap.json`; conflict UI; searchable shortcut
  editor. Presets: **Velocity** (default), Premiere-compatible, Resolve-compatible.
- v1 defaults match the brief: Space, J/K/L (with 2×/4×/8× repeats), I/O,
  arrows/Shift+arrows (1/10 frames), Home/End, S(plit)*, Q/W ripple-trim
  head/tail, V/B/H tools, M marker, Del ripple vs. Shift+Del lift, Ctrl+Z /
  Ctrl+Shift+Z, Ctrl+C/X/V/D, Ctrl+S, Ctrl+wheel zoom.
  *Note: brief said S=Split; Premiere users expect Ctrl+K, CapCut users expect
  B/S — the preset system resolves this rather than a debate.
- Focus-scoped maps (timeline vs. bin vs. text editing) with a global tier —
  the classic "space plays while renaming a clip" bug is designed out via
  focus scopes.

## 6. Theming & Polish Contract

- Single dark theme at v1 (light theme is a token swap later — all colors are
  semantic tokens from day one, no hard-coded hex in widget code).
- Per-monitor DPI aware v2 API; all metrics in DIPs; icons are SVG rendered to
  the exact device pixel grid.
- Full IME support in all text inputs (Qt gives this; the custom timeline
  never takes text input directly).
- Accessibility: Qt's UIA bridge for all chrome; the timeline exposes a
  virtual accessibility tree (clip list with names/times) — imperfect but
  present from v1, because bolting UIA onto a custom widget later never happens.
- Localization-ready (tr() everywhere, no string concat); English-only ship at v1.

## 7. UI ↔ Engine Contract (restated because it gets violated first)

The UI reads **snapshots** and emits **commands**. No engine object is ever
mutated from a Qt slot. Engine events arrive as queued, coalesced
notifications (playhead at 60 Hz max, meters at 30 Hz, thumbnails as-ready).
Violations are caught by a debug-build thread-assert on every engine entry point.
