# 03 — Project Format & Persistence

## 1. Container Decision: SQLite, not JSON files

A `.vep` project file is a **SQLite database** (WAL mode, `application_id` set,
`user_version` = schema version).

Why not JSON (challenging the implied default):

| Requirement | JSON file | SQLite |
|---|---|---|
| Crash-safe autosave | rewrite whole file, fsync dance, torn writes | single transaction, WAL, atomic by design |
| 1-hour project with 5k clips + 50k keyframes | parse everything on open | open instantly, load lazily |
| Versioning/migrations | ad-hoc | `user_version` + migration steps, testable |
| Incremental save | impossible (full rewrite) | dirty rows only |
| Tooling/debugging | text-diffable ✅ | `sqlite3` CLI, project-inspector tool |
| Long-term durability | fine | SQLite is an LoC-recommended archival format |

We recover JSON's one advantage (interchange/diffability) with **`File → Export
project as JSON`** — a canonical JSON serialization of the same schema, also
used by tests and future cloud sync. Entity payloads inside SQLite are typed
columns for queryable fields + a JSON blob for effect parameter sets (schema
churn isolation).

## 2. Schema (v1)

```sql
PRAGMA journal_mode=WAL;

CREATE TABLE meta      (key TEXT PRIMARY KEY, value TEXT);           -- app_version, created, modified, schema notes
CREATE TABLE settings  (key TEXT PRIMARY KEY, value JSON);           -- project settings, sequence format defaults

CREATE TABLE assets (
  id BLOB PRIMARY KEY,            -- UUIDv7
  kind INTEGER,                   -- video/audio/image/sequence-ref
  source_path TEXT,               -- absolute at save; see relinking
  relative_path TEXT,             -- relative to project file when possible
  content_hash BLOB,              -- xxh3-128 of file head+tail+size (fast identity)
  probe JSON,                     -- MediaInfo: streams, durations, codecs, color metadata
  proxy_state INTEGER, proxy_path TEXT
);

CREATE TABLE sequences (id BLOB PRIMARY KEY, name TEXT, format JSON, sort_order INTEGER);
CREATE TABLE tracks    (id BLOB PRIMARY KEY, sequence_id BLOB REFERENCES sequences,
                        kind INTEGER, idx INTEGER, flags INTEGER /* mute|lock|solo|hidden */,
                        name TEXT, color INTEGER);
CREATE TABLE clips (
  id BLOB PRIMARY KEY, track_id BLOB REFERENCES tracks,
  asset_id BLOB,                  -- or ref_sequence_id for nested timelines
  dst_start INTEGER, dst_len INTEGER,     -- ticks (1/48000 s)
  src_start INTEGER, src_len INTEGER,     -- source timebase units
  src_tb_num INTEGER, src_tb_den INTEGER,
  speed JSON,                     -- constant or speed-map
  flags INTEGER, z_order INTEGER,
  transform JSON, audio JSON      -- static props; animated ones live in keyframes
);
CREATE TABLE effects   (id BLOB PRIMARY KEY, clip_id BLOB REFERENCES clips,
                        effect_uid TEXT, effect_version INTEGER,
                        idx INTEGER, enabled INTEGER, params JSON);
CREATE TABLE keyframes (owner_id BLOB, param_id TEXT, tick INTEGER,
                        value JSON, interp INTEGER, bezier JSON,
                        PRIMARY KEY (owner_id, param_id, tick));
CREATE TABLE transitions (id BLOB PRIMARY KEY, track_id BLOB, at_clip_id BLOB,
                        effect_uid TEXT, duration INTEGER, alignment INTEGER, params JSON);
CREATE TABLE markers   (id BLOB PRIMARY KEY, sequence_id BLOB, tick INTEGER,
                        color INTEGER, label TEXT, note TEXT);
CREATE TABLE bins      (id BLOB PRIMARY KEY, parent_id BLOB, name TEXT, sort_order INTEGER);
CREATE TABLE bin_items (bin_id BLOB, asset_id BLOB, sort_order INTEGER);

-- Autosave journal: serialized commands since last full save
CREATE TABLE journal   (seq INTEGER PRIMARY KEY AUTOINCREMENT,
                        ts INTEGER, command JSON, snapshot_epoch INTEGER);

CREATE INDEX idx_clips_track ON clips(track_id, dst_start);
CREATE INDEX idx_kf_owner    ON keyframes(owner_id, param_id, tick);
```

## 3. Save / Autosave / Crash Recovery

Three cooperating mechanisms:

1. **Explicit save (Ctrl+S):** write dirty entities in one transaction, clear
   the journal, bump `meta.modified`.
2. **Command journal (continuous):** every committed command is appended to
   `journal` within ~250 ms (debounced, off the UI thread). Cost: microseconds.
   This is the crash-recovery source of truth — recovery = load last saved
   state, replay journal, offer "Recover to your last edit?".
3. **Snapshot autosave (every N minutes, default 3):** full dirty-entity flush
   into the same file (WAL makes this non-blocking for the UI thread's next
   command) + a rotating copy `project.vep.autosave/NNN.vep` (default keep 10)
   for the "project file corrupted / user error" tier. These rotating copies
   double as the **version history** feature ("File → Restore previous
   version"), with named manual checkpoints stored the same way.

Crash detection: a lockfile/PID sentinel + clean-shutdown flag in `meta`.
On unclean open, run recovery flow before showing the timeline.

## 4. Media Relinking

- Assets store absolute + project-relative path + `content_hash` +
  size/mtime. On open, resolve in that order: relative path → absolute path →
  user-directed search folder scanned by hash match.
- Missing media becomes an **offline asset placeholder** (red slate) — the
  timeline stays fully editable. This must be in v0.1; retrofitting offline
  media handling is notoriously painful.

## 5. Forward/Backward Compatibility Policy

- `user_version` gates opening. Newer-major files refuse politely.
- Older files migrate via ordered, individually tested migration steps;
  migration always writes to a copy first.
- Unknown `effect_uid`s survive round-trips untouched (parameters are opaque
  JSON) — a project touched by a newer version with new effects does not lose
  them when edited in an older version, it just shows "unsupported effect".
- The JSON export schema is documented in `docs/formats/project-json.md`
  (written when implemented) and versioned with the same number.

## 6. Templates, Presets, Recent Projects

- **Project template** = a normal `.vep` opened copy-on-write.
- **Presets** (effects, text styles, export settings) = small JSON documents in
  `%APPDATA%/Velocity/presets/`, shareable as files.
- **Recent projects** list + thumbnails in `%APPDATA%/Velocity/state.db`
  (SQLite again; never in the registry).
- Caches (proxies, waveforms, thumbnails, render cache) live **outside** the
  project file in a per-project cache directory (see [09](09-cache-strategy.md)) —
  the project file stays small, portable, and mail-able.
