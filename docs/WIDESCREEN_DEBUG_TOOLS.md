# Widescreen debug tooling plan

Distilled 2026-08-15 from three sources:

- `docs/WIDESCREEN_HANDOFF.md` — the seven open issues and release gates;
- the SuperZSNES emulator effort (`D:\Downloads\DKLR\DKC-Widescreen-358x224`,
  38 BepInEx plugins + 13 offline tools + `docs/WORKLOG.md` bug history);
- what the snesrecomp runtime already provides (WsShadow stats/debug cells,
  `framedump.c`, snapshots, input playback, the DKC2Recomp script set).

The recomp's structural advantage: we own the process. Atomic WRAM snapshots
are a memcpy of `g_ram`, frame boundaries are exact, input replacement and
replay-from-root already exist, and headless runs uncapped. Most of the
emulator effort's hardest-won infrastructure (Harmony hooks, IL signature
gates, bridge servers) is simply unnecessary here — what transfers is the
*method*: atomic evidence, first-divergence search, lifecycle semantics,
non-conclusion vocabulary, and 3x byte-identical repeat gates.

Cross-cutting rules adopted from the emulator worklog:

- every tool is default-off and provably inert when off;
- transitions, not per-frame dumps, for long recordings;
- symbolic PC/WRAM labels are grounded in byte-exact assertions against the
  verified ROM before any conclusion is published;
- OAM tooling always labels WRAM shadow ($0200/$0400) vs PPU OAM and reads
  both (the X-high bug produced a wrong published conclusion from reading one
  copy one VBlank early);
- capture targets never silently substitute surfaces (the DKC2-era shimmer
  bug was misdiagnosed for a session because `composed` fell back to `main`);
- actors are matched by source record + world position, never by slot;
- fresh-entry evidence is kept separate from loaded-state evidence (two
  emulator bugs were unprovable from save states that had serialized
  already-corrupt VRAM);
- raw bytes gate, images illustrate ("PNG comparison is not accepted in
  place of raw BG1 bytes").

## Tier 0 — substrate (build first; everything else consumes these)

### 1. Per-frame widescreen decision trace (`DKC1_WS_TRACE`)
The record the handoff already specifies, as default-off JSONL from the
headless host, one object per frame: host+SNES frame; mode/entrance/fade;
source-signature fields (map bank/base, metatile base, stream VRAM base);
BGMODE/BGSC/main/sub/wide-layer mask/terrain layer; camera + all four scroll
pairs; per-layout `matches/decodable` calibration scores; selected layout,
grace/miss counters; which of {WsShadowReset, cold init, WsShadowFrame,
prefill, BG3 repeat, edge extension, centered fallback} ran;
per-layer world keys and margin tile counts; `WsShadowGetMarginStats`
deltas; and separate hashes of left margin / native center / right margin
plus per-BG-plane margin hashes.
**Serves:** open issues 1–3 directly (the immediate question: *which exact
frame and reason changes retained margin pixels while VRAM/OAM are
identical*). **Reuses:** DKC2's `widescreen_frame` trace pattern.
**Cost:** ~1 day. Build nothing else calibration-related until this exists.

### 2. Route harness: wait-predicates, snapshots, fresh-entry recipes
Formalize `SNESRECOMP_INPUT_PLAY` into recipe JSON (port of
`DKCLevelAutomation`): steps are `automation` / `checkpoint`, with
`wait_wram` predicates (mask/shift/signed compares) instead of fixed frame
counts — this kills the map-entry timing roulette permanently. Add:
per-frame atomic WRAM dump ranges (`DKC1_WRAM_DUMP=7500-7600`, gz stream +
SHA-256 JSONL index, reusing `framedump.c`); `DKC1_SAVESTATE_INPUT` /
`DKC1_SAVESTATE_SAVE_AT` via the runtime's `RtlSaveSnapshot`/`RtlLoadSnapshot`
so routes can anchor mid-game; a named library of fresh-entry recipes
(boot→file→map→each level) since fresh entry is the only valid evidence for
initializer bugs. **Serves:** every other tool; open issues 5–6.
**Cost:** 1–2 days.

### 3. Stock-vs-wide first-divergence locator
Port of `DKCFirstDivergenceLocator` / `DKCDualRuntimeDifferential`, much
simpler natively: run the same recipe under `DKC1_WIDESCREEN=0` and `=1`,
stream per-frame WRAM, compare with order-sensitive window hashes (endpoint
comparison misses transient divergences that reconverge), binary-search to
the exact first frame, then confirm by independent re-replay. Report BOTH
`firstRawFrame` (any of 128 KiB) and `firstUnexpectedFrame` (predicate-
selected), with include-groups ported from the emulator: `core_gameplay`,
`actor_pool`, `object_bookkeeping` ($192B table), `scanner`,
`section_controller`, `camera_and_bounds` — the last filtered through an
expected-widescreen profile so legitimate presentation deltas don't mask the
first real gameplay diff. **Serves:** open issue 4 (wide-vs-native WRAM
divergence at frame 7,600 is currently unclassified). **Cost:** 2–3 days.

## Tier 1 — bug-class instruments

### 4. Object lifecycle tracer + prefetch phase auditor
The emulator program's crown jewels (`DKCObjectLifecycleTracer`,
`DKCObjectPrefetchPhaseAuditor`), ported to native sampling of `g_ram`:
per-frame decode of the bank-$BD actor pool, the $192B–$1A2A bookmark
table, scanner window/cursors, type-9 section state, and the authored
entrance list with decoded type-5 children; emit transitions only; gate on
gameplay identity so map/menu WRAM is never decoded as actors. Allocator
semantics hooked at the opcode-verified PCs ($BDF3B1/$BDF3D2 exhaustion,
$BDF3B5/$BDF3D6 success — verify bytes against the ROM first; the emulator
shipped these inverted once). Phase auditor aligns stock/wide episodes by
source record (never slot), compares identity/position/motion/state/
animation plus the conservative collision-scratch range $0C35..$109D at the
first stock allocation frame, and uses the honest vocabulary:
`harmless_visual_prefetch`, `behavior_phase_advancement`,
`wide_persists_stock_culls` (= indeterminate, never "harmless").
**Serves:** open issues 4–5; the grouped-child-retry and early-activation
release gates. **Cost:** 3–5 days; highest-value single instrument.

### 5. Margin provenance overlay + plane isolation (desktop host)
Debug hotkeys in `dkc1_desktop`: BG1/BG2/BG3/OBJ isolation toggles, and a
false-color margin mode painting every margin tile by its WsShadow source —
captured-from-VRAM / prefill guess / blank fallback / periodic fold /
edge repeat / raw-VRAM fallback (the dangerous one) — via
`WsShadowDebugCell` plus a small per-source tag extension. Also an on-screen
strip showing terrain_ready, layout, calibration score, world keys.
**Serves:** open issues 1–2 visually — the "inconsistent dark shapes near
margin edges" become instantly attributable to a source class.
**Cost:** 1–2 days.

### 6. Region-aware frame/plane differ (raw-bytes oracle)
Port `compare_frames.py` and the framebuffer-oracle discipline: split every
wide capture into left-margin / native-center / right-margin; gate on
center == native oracle pixel-exact and hash margins separately; declare a
comparison invalid when raw PPU inputs differ (only equal inputs isolate a
renderer difference). Add headless dumps of the live rolling tilemap and
the WsShadow world-keyed store as raw bytes for direct comparison against
the ROM-decoded expectation — three-way: ROM decode vs VRAM vs shadow.
**Serves:** open issues 1, 6; the transition release gate ("no prior world
tiles may survive in either margin"). **Cost:** ~1 day.

### 7. OAM inspector with shadow-vs-PPU dual view and wrap detector
Per-frame dump/compare of WRAM OAM shadow ($0200/$0400) AND PPU OAM,
labeled, with 9-bit X decode, size bits, and world back-projection via the
camera; automatic flag for entries whose art X sits in [256, 256+extra] but
whose X-high bit is 0 (the wrap signature from emulator bugs 5–6), and for
left-margin entries wrapping from negative X. Bakes in the two-VBlank rule:
a one-frame PPU/shadow disagreement is lag, not a bug.
**Serves:** margin sprite work (cull adapters, rope/banana private paths).
**Cost:** ~1 day.

## Tier 2 — route/regression infrastructure

### 8. Regression recipes + closure contracts + 3x repeat gate
Port `run_regression.py` / `run_softlock_closure.py` / the offline
`verify_*` pattern: recipe checkpoints carry `expect` blocks; contracts are
machine-readable JSON that reject any `write_wram`; the gate is three
repeats with byte-identical full-WRAM SHA-256 at every checkpoint. Seed with
the emulator's proven route set (they port as route *definitions*, not
states): cave banana position AND pickup, cave exit traversal, barrel-cannon
type-5 child retry, Slipslide type-9 progression, Croctopus/Poison
completion, K. Rool fresh entry. **Serves:** open issue 5 and most release
gates. **Cost:** 2–3 days plus route re-recording.

### 9. Input flight recorder + macro minimizer
The tool the emulator effort wished it had on day one: rolling input ring +
periodic runtime snapshot in the desktop host, one key exports a repro
bundle (anchor snapshot + per-frame masks + final WRAM hash). Companion
ddmin minimizer (port `DKCMacroMinimizer`) shrinking failing macros with
button-transition preservation and 3x hash-confirmed candidates,
treating any nondeterminism as an abort. Trivial natively (~200 lines +
port). **Serves:** every future playtest report. **Cost:** 1–2 days.

### 10. Whole-game level sweep harness
The emulator effort's biggest structural gap, and far more achievable here:
headless, uncapped, iterate every entrance — fresh entry, then either a
simple scripted traversal or a camera-probe mode (force the presentation
camera along the level while calibrating/decoding margins every frame) —
recording calibration scores, layout locks, margin-stat rawFallback counts,
wide-vs-stock WRAM divergence class, and margin continuity per level.
Nightly HTML report. Finds unknown layouts (bonus rooms, mine carts, water,
vertical) before players do. **Serves:** open issues 5–6 at coverage scale.
**Cost:** 3–4 days once tools 2/3/6 exist.

### 11. Unified timeline viewer
Port `DKCObjectSectionTimeline`'s self-contained HTML (camera/player paths,
per-record eligibility/booking/actor-lifetime bands, scanner decisions,
first-anomaly marker, clickable checkpoints) with schema adapters for the
recomp's JSONL streams. The emulator team correlated these streams by hand
for weeks before building it. **Cost:** ~1 day (mostly adaptation).

### 12. Indirect-dispatch target logger (the `$BE8179` gap)
Pre-opcode hook on unauthorized dispatch sites logging the actual computed
targets seen at runtime, aggregated into proposed `indirect_dispatch`
cfg contracts. Closes open issue 7 with evidence instead of guesswork, and
generalizes to any future dispatch gap. **Cost:** ~half a day.

## Suggested order against the current open issues

1 → 5 → 6 (attack the margin nondeterminism at frame 7,600 with evidence)
→ 3 → 4 (classify the wide-vs-native WRAM divergence and early activation)
→ 2 → 8 (make the object-fix routes provable) → 12 → 9 → 10 → 11.
