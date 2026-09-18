# Widescreen debug tooling plan

## Native iOS portability check

`tools/verify_ios_simulator.py` runs the existing headless implementation inside
a diagnostic UIKit app and compares three fresh-boot replays with three desktop
replays. It records frame, WRAM, VRAM, CGRAM, both OAM and audio hashes plus raw
evidence. `DKC1_IOS_DIAGNOSTICS` is OFF in device releases; the new UI capture
overrides are compiled out. `--render-width 418` compares native phone-width
rendering (default 256). Headless/layer capture accept the bounded, optional
`DKC1_RENDER_WIDTH` override. The fresh-entry stress tool accepts an explicit
`--render-width`, keeps its native twin at 256, and grades the matching margin.
See [iOS commands, tested scenes and incomplete full-game floor](IOS.md)
and the canonical tool catalog for invocation and save-container isolation.
The paused `DKC1_IOS_QA_ROLL_Y_B` display probe shows the simultaneous Y/B
touch contribution; it does not automate or certify physical thumb input.
`--verify-graphics` runs the existing Metal output/cache oracle inside the
diagnostic iOS app. `DKC1_IOS_QA_UPSCALER` and
`DKC1_IOS_QA_UPSCALING_MENU` select a filter and its live-preview sheet for
paused source-identical screenshots. See the canonical catalog for inputs,
outputs and the distinction between display probes and physical touch QA.

Optional Dixie verification uses the existing native route runner with
`contracts/dixie-jungle.json` and `build/dkc1_dixie_headless.exe`; do not run
its patched cartridge through the stock host or assume that a native pass
promotes widescreen. `build_host_dixie.bat`, `build_host_dixie_tools.bat`, and
the opt-in CMake `DKC1_BUILD_DIXIE_VARIANT` SDL sibling are documented in
[`DIXIE_MOD.md`](DIXIE_MOD.md). Historical failures are recorded in
[`DIXIE_QA_2026-09-13.md`](DIXIE_QA_2026-09-13.md). The map mapping fix and
immutable exact-state/fresh-entry evidence are in
[`DIXIE_MAP_FIX_2026-09-14.md`](DIXIE_MAP_FIX_2026-09-14.md).
`contracts/dixie-map-refresh.json` requires that report's preserved root via
`DKC1_SAVESTATE_INPUT`; it verifies normal map reload, resources and navigation
with three identical native replays. It does not promote widescreen coverage.
The [v0.0.13 release record](RELEASE_0.0.13.md) adds extracted-package graphics,
Dixie level-entry and stock cartridge-save/cold-restart verification. The
canonical catalog documents the allowlisted Windows packager and the optional
DMA attribution taps included in the pinned engine.
`contracts/dixie-stomp.json` adds fresh-boot stomp and landing closure.
The Windows `--haptics-test` uses the actual worker and an SDL virtual device
to verify pulse/disable/stop without physical motors. See
[`DIXIE_HAPTICS.md`](DIXIE_HAPTICS.md) for ROM-byte, recorded-frame and UI tests.

Windows v0.0.12 adds `verify_ingame_saves.py` for three-repeat SRAM persistence
and cold-restart checks. Its isolated user-directory contract, audio comparison
exclusions and controlled Candy fixture are documented in
[`INGAME_SAVES.md`](INGAME_SAVES.md) and the canonical tool catalog. Desktop
debugging now requires private save directories to avoid updating user SRAM.

Windows v0.0.10 exposes the shared SDL host diagnostics and three ROM-free
CTest targets. See the Windows section of the canonical tool catalog and
[`WINDOWS_RELEASE.md`](WINDOWS_RELEASE.md). This does not promote a new
widescreen capability or change the 40-entrance gate.

> **This is the historical build-out plan and session chronicle.** For
> the current symptom-to-fix workflow with the finished tool suite,
> start at **`docs/WIDESCREEN_PLAYBOOK.md`**; the tool reference is
> `.claude/skills/dkc1-tools/TOOLS.md`.

Distilled 2026-08-15 from three sources:

- `docs/WIDESCREEN_HANDOFF.md` — the seven open issues and release gates;
- the LEGACY SuperZSNES emulator effort (in-repo copy:
  `reference/legacy-widescreen/`; original at
  `D:\Downloads\DKLR\DKC-Widescreen-358x224` — prior art only, see the
  dkc1-tools skill's two-efforts warning), 38 BepInEx plugins + 13
  offline tools + `docs/WORKLOG.md` bug history;
- what the snesrecomp runtime already provides (WsShadow stats/debug cells,
  `framedump.c`, snapshots, input playback, the DKC2Recomp script set).

The recomp's structural advantage: we own the process. Atomic WRAM snapshots
are a memcpy of `g_ram`, frame boundaries are exact, input replacement and
replay-from-root already exist, and headless runs uncapped. Most of the
emulator effort's hardest-won infrastructure (Harmony hooks, IL signature
gates, bridge servers) is simply unnecessary here — what transfers is the
*method*: atomic evidence, first-divergence search, lifecycle semantics,
non-conclusion vocabulary, and 3x byte-identical repeat gates.

## Implementation status

- **The staged 65816 IR has a fail-closed read-only viewer.** Stages 1–3
  (lossless decode, CFG/SSA/width facts, and typed memory) are gated by
  `tools/ir_validate.py`. `tools/irview.py ADDR|NAME [--ssa]` renders one
  instruction-index seed group. Because those seed labels are not proof of a
  closed routine, any contiguous fallthrough into another seed is shown as an
  exact `TAIL-FALLTHROUGH` and the view is labeled partial; unresolved indirect
  successors and other CFG/SSA boundary problems are also prominent rather
  than silently omitted.

- **The evidence-summary tools now fail closed.**
  `tools/capability_manifest.py` accepts scene evidence only from successful
  sweep routes and will not emit `proven` when calibration is incomplete or
  any raw fallback, aggregate blank serve, gameplay pillarbox, or unstable
  margin was recorded. The aggregate sweep does not carry a per-blank oracle,
  so no rate-based blank allowance is used. Every blocker is preserved in the
  manifest. `tools/build_profile_corpus.py` clears prior route profiles and
  publishes a corpus only if every standalone route exits successfully and
  produces a nonempty valid JSONL profile. `tools/profile_diff.py` counts the
  exact 2,558 address-bearing `CpuState` declarations in `recomp/funcs.h`.
  `tools/impact.py` takes callers from the exported structured IDA call graph,
  falling back only to exact control-flow operands in the instruction index;
  a name appearing in pseudocode text is not caller evidence.

- **The visible desktop host is now the primary interactive debugger.** It
  accepts the same `DKC1_SCRIPT`, native snapshot, input playback, checkpoint,
  WRAM dump, OAM, lifecycle, input-recording, and widescreen-trace environment
  variables as the batch host. A separate right-side panel shows route/input,
  mode/level/entrance, camera bounds, scanner range, section state, and which
  evidence taps are armed without covering or modifying the captured game
  framebuffer. `F7` pauses/resumes and `F8` advances exactly one frame while
  paused. A completed or failed route pauses in the visible window for
  inspection rather than disappearing. The headless executable remains the
  right choice for stock-vs-wide replay, minimization, and level sweeps.

- **Tool 1 is implemented.** Set `DKC1_WS_TRACE=1` for
  `dkc1_ws_trace.jsonl`, or set it to an explicit output path. The trace is
  default-off and performs no file I/O or frame hashing while disabled.
  Schema: `docs/schemas/dkc1-ws-frame-v1.schema.json`.
- `tools/analyze_ws_trace.py` summarizes decision counts, unsafe raw-fallback
  frames, prefill refreshes, and the exact frames where margin hashes change
  while VRAM, PPU OAM, and WRAM OAM stay identical.
  Shadow-margin hashes use the geometric margin extent on calibration-grace
  frames, so a frame without a new prefill cannot disappear from the trace.
- **Tools 5 and 6 now have their first implementation.** `F1` in
  `dkc1_desktop` toggles margin provenance; `F2` restores the composite and
  `F3`–`F6` isolate BG1/BG2/BG3/OBJ. The same overlay is available headlessly
  with `DKC1_WS_PROVENANCE=1`. `tools/compare_widescreen_regions.py` hashes
  left/native-center/right independently, requires an exact center by
  default, emits a red-pixel diff, and can gate a comparison on matching
  VRAM plus both PPU/WRAM OAM trace hashes.
- **Tool 2 is implemented (schema v1).** The headless host supports checksum-indexed,
  range-selective atomic WRAM captures. Set `DKC1_WRAM_DUMP=7500-7600`,
  `DKC1_WRAM_DUMP_PATH=<raw.bin>`, and optionally
  `DKC1_WRAM_DUMP_RANGES=0000-01ff,192b-1a2a`. The adjacent JSONL manifest
  and frame index use `docs/schemas/dkc1-wram-dump-v1.schema.json`; verify
  payload length, offsets, ranges, and every SHA-256 with
  `tools/verify_wram_dump.py`. Native snapshot input/output is available via
  `DKC1_SAVESTATE_INPUT`, `DKC1_SAVESTATE_OUTPUT`, and optional 1-based
  `DKC1_SAVESTATE_SAVE_AT`; input and output paths must differ so a route
  cannot overwrite its immutable anchor. `tools/run_route_recipe.py`
  validates `dkc1.route.v1` JSON, compiles it to the native frame-boundary
  runner, and supports fixed input, 1/2/4-byte `wait_wram` predicates,
  masks, shifts, signed comparisons, held input, timeouts, and named raw
  checkpoints. The route manifest hashes the recipe, verified ROM, runner,
  and optional anchor. Native snapshots remain the preferred replay format.
  SuperZSNES v0.230 `.szst` files can now be converted with
  `tools/SuperZSNESStateExporter` and loaded through
  `DKC1_SUPERZSNES_STATE=<bundle-directory>`. The bridge restores exact raw
  machine memories and mapped CPU/PPU/APU state, then reconstructs DSP
  interpolation history; the first audio buffer is therefore not an exact
  cross-runtime oracle. `DKC1_SAVESTATE_INPUT` and
  `DKC1_SUPERZSNES_STATE` are mutually exclusive.
- **Tool 3 is implemented.** `tools/first_divergence.py` runs an identical
  native/wide route twice, fingerprints every full-WRAM frame in order, then
  captures and classifies raw windows around the first differing frame. A
  real 7,600-frame Jungle route currently reports `no_divergence` rather than
  hiding an expected camera/window delta.
- **Tool 7 is implemented.** Set `DKC1_OAM_LOG=<prefix>` and inspect it with
  `tools/oam_inspect.py`. Capture metadata gates evidence to active gameplay,
  excludes forced blank and menu/map OAM, recognizes DKC's tile-`$FF` unused
  marker, tolerates the normal DMA pipeline, and reports X-high loss only
  when the same WRAM-shadow entry supplies direct contradictory evidence.
  The 7,600-frame Jungle oracle was clean: zero X-high loss suspects, with
  valid left/right margin entries still reported descriptively.
- **Tool 16 is implemented.** `tools/bisect_transition_contamination.py`
  compares a route-history frame with a zero-frame render of an exact snapshot
  of that same machine state. The reload resets only host-owned widescreen
  history, while WRAM/VRAM and the native 256-pixel center are required to
  remain exact. A margin-only difference is therefore classified as retained
  transition contamination; raw-state or center differences fail closed.
  The supplied good/bad window is bisected, the predecessor is rechecked, and
  the boundary is repeated three times by default. Reports keep
  `analysis_valid` separate from the release-gate `passed`: deterministic
  contamination is valid evidence but still fails the gate. Reports use
  `docs/schemas/dkc1-transition-contamination-v1.schema.json`.
- **The transition sentinel is implemented.**
  `tools/transition_contamination_sentinel.py` turns that single-window
  bisector into a route-wide regression gate. It discovers hard scene,
  source, identity, reset, and cold-start boundaries from `DKC1_WS_TRACE`,
  samples each boundary at configurable follow-up offsets, and renders the
  exact same snapshot with retained and cold host history. Every sample
  compares WRAM, VRAM, CGRAM, PPU OAM, and WRAM OAM before independently
  hashing BG1, BG2, BG3, OBJ, and composite across left margin, native center,
  and right margin. Machine-state or center differences fail closed; a
  margin-only difference is reported as retained-layer contamination. The
  first failure preserves the snapshot, raw planes, traces, isolated layers,
  logs, JSON, and an HTML timeline. Schema:
  `docs/schemas/dkc1-transition-sentinel-v1.schema.json`.

  Example:

  ```powershell
  python tools\transition_contamination_sentinel.py `
    --runner build\dkc1_headless_tools.exe `
    --layer-capture build\dkc1_layer_capture.exe `
    --rom D:\private\DKC1_USA1.sfc `
    --bundle build\visible-flight\capture-f00000464-... `
    --output build\transition-sentinel
  ```

Provenance colors are: green = captured/authentic history, cyan = ROM
prefill, magenta = proven periodic fold, gray = verified transparent blank,
blue = explicit valid 64-column continuation, red = unsafe circular-VRAM
fallback, yellow = native edge repeat. The wash is
applied only to side margins; the native center remains byte-identical. The
region report schema is `docs/schemas/dkc1-ws-regions-v1.schema.json`.

```powershell
python tools\compare_widescreen_regions.py native.ppm wide.ppm `
  --extra 43 --json-out regions.json --diff-out regions-diff.ppm
```

Example:

```powershell
$env:DKC1_WIDESCREEN = '1'
$env:DKC1_WS_TRACE = "$env:TEMP\dkc1-ws.jsonl"
.\build\dkc1_snesrecomp_headless.exe C:\private\dkc1.sfc 7600
python tools\analyze_ws_trace.py $env:TEMP\dkc1-ws.jsonl `
  --json-out $env:TEMP\dkc1-ws-summary.json
```

Atomic WRAM capture example:

```powershell
$env:DKC1_WRAM_DUMP = '7500-7600'
$env:DKC1_WRAM_DUMP_PATH = "$env:TEMP\route.wram.bin"
$env:DKC1_WRAM_DUMP_RANGES = '0000-01ff,192b-1a2a'
.\build\dkc1_snesrecomp_headless.exe C:\private\dkc1.sfc 7600
python tools\verify_wram_dump.py $env:TEMP\route.wram.bin
```

Recipe validation and execution:

```powershell
python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --validate-only --script-out $env:TEMP\route.script

python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --rom C:\private\dkc1.sfc `
  --runner .\build\dkc1_snesrecomp_headless.exe `
  --session-dir $env:TEMP\dkc1-route-smoke

# Interactive: launch the same route in the visible debugger and return.
# It pauses on the final frame so the result can be inspected.
python tools\run_route_recipe.py recipes\fresh-entry-smoke.json `
  --rom C:\private\dkc1.sfc `
  --visible `
  --session-dir $env:TEMP\dkc1-route-visible
```

Checkpoint names are restricted to safe filenames and duplicates are
rejected. A timed-out predicate exits nonzero and cannot produce later
checkpoint evidence. Named checkpoints contain full 128 KiB WRAM plus
SHA-256 for WRAM, VRAM, WRAM OAM shadow, and PPU OAM. The smoke route was
accepted only after three independent runs produced byte-identical WRAM.

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
**Status: implemented (schema v1).**
The record the handoff already specifies, as default-off JSONL from the
headless host, one object per frame: host+SNES frame; mode/entrance/fade;
source-signature fields (map bank/base, metatile-definition bank/base, stream
VRAM base);
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
**Status: implemented (schema v1); the route library is intentionally small.**
Formalize `SNESRECOMP_INPUT_PLAY` into recipe JSON (port of
`DKCLevelAutomation`): steps are `automation` / `checkpoint`, with
`wait_wram` predicates (mask/shift/signed compares) instead of fixed frame
counts — this kills the map-entry timing roulette permanently. Add:
per-frame atomic WRAM dump ranges (`DKC1_WRAM_DUMP=7500-7600`, raw stream +
SHA-256 JSONL index); `DKC1_SAVESTATE_INPUT` /
`DKC1_SAVESTATE_SAVE_AT` via the runtime's `RtlSaveSnapshot`/`RtlLoadSnapshot`
so routes can anchor mid-game; a named library of fresh-entry recipes
(boot→file→map→each level) since fresh entry is the only valid evidence for
initializer bugs. **Serves:** every other tool; open issues 5–6.
The engine, schema, native snapshots, checksum-indexed WRAM ranges, manifest,
and smoke recipe are present. Adding authored routes for every level remains
coverage work rather than a missing substrate. **Cost:** implemented.

### 3. Stock-vs-wide first-divergence locator
**Status: implemented; initial 7,600-frame route is WRAM-identical.**
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
**Status: implemented; placed-actor behavior guard remains experimental and
default-off.**
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
release gates.

The native lifecycle stream also has a focused
`dkc1.prefetch-phase.v1` schema for candidate/suppression/fallback-hold/release
transitions. `DKC1_WS_FORCE_FALLBACK_FRAME` supplies a deterministic,
cartridge-state-neutral one-frame fallback injector. Run
`tools/verify_prefetch_soft_fallback.py` over at least three route directories;
it rejects context resets during the fallback, missing or duplicate actor
transitions, release outside the reconstructed stock window, and any
cross-run semantic mismatch. Accepted evidence is under
`build/phaseguard-v5-soft-fallback/`. The remaining release blocker is not
tooling: actors that legitimately remain visible in a wide margin after stock
culls them still require an explicit gameplay policy and route oracle.

`DKC1_WIDESCREEN_EDGE=reflect|bars|shift|glide` selects the level-wall presentation on every host (default `glide`; the macOS View > Level Edge menu sets the same policy and remembers it); the widescreen trace records the active policy and the visible margin per side in its `camera` object as `edge`, `left`, and `right`. Running the same state under `shift` reproduces the pre-policy build exactly, which makes it the A/B reference for wall reports.

Fresh-entry tracking must seed actor identities from the pre-scanner frame
boundary, not from the first behavior dispatch. The latter occurs after the
widened scanner may already have allocated a margin-only record and therefore
misclassifies the new actor as left-censored. The Tree Top `$02` proof is
`build/first-divergence-treetop-righty-phaseguard-v2/`: allocation at relative
frame 92, stock-window release at frame 190, and no premature hit/exit.
Matrix tests must pass `fresh_entry_stress_sweep.py
--prefetch-phase-guard`; the harness records the setting and explicitly sets
the child environment after removing any ambient value.

Fresh-entry stress also supports `--align-gameplay-ready`. Native and wide
entry initialization run independently, then the tool starts the shared input
only after the resolved scene and active Kong fields match. It retains both
ready snapshots, their different host-frame counts, and the exact mismatch
fields when alignment is impossible. Use the default 64 neutral settling
frames; four frames was proven too short by Tree Top Town's one-frame entrance
walk skew. Do not feed alignment-rejected branches into lifecycle triage as if
they had run; the triager records them as skipped with their rejection reason.

The guard switch is an experiment, not a release recommendation. It fixes the
aligned Winky's Walkway `$02->$04` allocation failure, but the full guarded
matrix changes Misty Mine survival relative to an aligned unguarded control.
This is evidence that freezing a real early-allocated actor still changes pool
and ordering semantics. The preferred replacement is a native-width gameplay
scanner plus a separate, read-only presentation proxy for authored records in
the extra margins.

Use `--prefetch-phase-guard --prefetch-transaction-debug` only for proxy
research. It emits structured `dkc1.prefetch-transaction.v1` rows during both
gameplay-ready alignment and the stress branch. Analyze them with
`tools/analyze_prefetch_write_sets.py ... --json-out report.json`. A proxy
candidate may retain only its own host-owned actor fields and presentation
output; writes to another actor, `$192B` bookkeeping, or unclassified global
WRAM are release blockers. Direct-page `$0000-$01FF` and the opcode-grounded
`$08AB` draw-loop temporary remain visible as `scratch` but are discarded by
the enclosing transaction.

The first renderer-backed proxy is now implemented and has its own evidence
gate.  `tools/verify_margin_proxy_ab.py --on-dir <proxy-on> --off-dir
<proxy-off>` compares one aligned frame and permits differences only in the
documented presentation domains: DKC renderer scratch, WRAM OAM shadow, and
the sprite graphics-upload queue.  It rejects any gameplay-WRAM change, any
audio change, a missing WRAM-shadow/PPU-OAM handoff, or pixels entering the
protected native center.  The Winky source-`$02` fixture passes three
byte-identical repeats under `build/winky-proxy-repeat-gate-20260816/`.

When borrowing a free normal-actor slot, preserve `$0AB1,x`: despite its
actor-strided address it is the global `NorSpr_DrawOrderIndexLo` array, not
host-owned proxy state.  Zeroing it silently drops the actor before
`CODE_BBA849`.  Also evaluate visual onset with both OAM copies: the WRAM
shadow changes during the draw and PPU OAM follows on the next VBlank.

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
`tools/verify_shadow_localization.py` additionally validates that high-world
absolute keys project into stable per-layer cache windows, that 512px/256px
origins preserve rolling-map parity, and that terrain margins have no misses.
This catches the bonus/vertical-stage 4:3 cutoff that looked like a BG-width
policy failure.

`tools/detect_legacy_width_cull.py` is the focused same-frame plane check for
this failure class. Layer-capture schema v2 emits a backdrop-only P6 plus a
backdrop-subtracted P5 occupancy mask for every composite/BG/OBJ surface, so
shared sky/fixed-color pixels cannot make an empty OBJ margin look repeated.
The auditor locates the centered 256-pixel boundaries and reports empty side
margins, an exact opposite-edge copy, and boundary transitions that are
outliers relative to nearby columns. It evaluates both the full plane and
16-line bands, catching a foreground that stops only over part of the screen.
Only an empty margin is a hard failure by itself. Repeat and seam findings are
diagnostic leads because a bounded, authored periodic plane (notably DKC1
BG3) can legitimately repeat without producing a visible discontinuity. The
Expresso Bonus quicksave audit is retained at
`build/bonus-current-masked-layers/legacy-width-audit.json`.

The live blank-band detector applies the same boundary discipline: it starts
at each centered native/margin seam and walks outward until the first
structured column. It deliberately does not count a flat interval beginning
later inside the margin. The Expresso-return route contains the regression
oracle—a 27-pixel authored transparent BG2 opening beginning four pixels past
the right seam. Treating arbitrary flat margin columns as a cutoff falsely
flagged it; raw VRAM matched a normal fresh Jungle map exactly. The corrected
12-action x 2-repeat matrix is
`build/bonus-bandscan-v2-full-matrix/report.json` and has zero cull events.
**Serves:** open issues 1, 6; the transition release gate ("no prior world
tiles may survive in either margin"). **Cost:** ~1 day.

### 7. OAM inspector with shadow-vs-PPU dual view and wrap detector
**Status: implemented and live-route validated.**
Per-frame dump/compare of WRAM OAM shadow ($0200/$0400) AND PPU OAM,
labeled, with 9-bit X decode, size bits, and world back-projection via the
camera; automatic flag for entries whose art X sits in [256, 256+extra] but
whose X-high bit is 0 (the wrap signature from emulator bugs 5–6), and for
left-margin entries wrapping from negative X. Bakes in the two-VBlank rule:
a one-frame PPU/shadow disagreement is lag, not a bug.
Every OAM index row also records the PPU `rangeOver` (33rd sprite) and
`timeOver` (35th fetched sliver) latches. Headless run summaries count frames
with each latch, checkpoints retain both booleans, and an F9 bundle records
their final-frame state. This distinguishes malformed OAM from valid OAM whose
8x8 pieces were discarded by the scanline budget.
**Serves:** margin sprite work (cull adapters, rope/banana private paths).
`tools/verify_vertical_rope_margins.py` is the two-sided route gate: it
requires native-to-margin crossings, correct 9-bit X, no low-byte alias copy,
no mismatch beyond the normal one-VBlank OAM handoff, and three byte-identical
OAM streams per side.
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
**Status: rolling visible-host recorder implemented; minimizer implemented
separately.** Set `DKC1_FLIGHT_RECORDER=1` before launching the desktop host.
It retains about one minute of resolved controller input plus a native snapshot
anchor every 300 frames entirely in memory. Pressing **F9** exports a versioned
repro bundle containing the covered anchor and current snapshots, exact
per-frame input masks, full WRAM/VRAM/CGRAM, both WRAM-shadow and PPU OAM, and
SHA-256 provenance. `tools/verify_flight_bundle.py` validates the bundle and can
replay it through a supplied runner to prove the final 128 KiB WRAM hash.
Native state loads are explicit timeline boundaries.  F12, the file-picker
load path, and scripted `state_load` now discard every pre-load input/anchor
and immediately capture the loaded machine as the new replay root.  This was
added after a real bonus-stage bundle claimed to cover host frames 300–3625
even though F12 had replaced the machine near the end; the bundle was
internally hash-valid but could never replay its final WRAM.  A quickload must
never be represented as ordinary controller history.
`DKC1_FLIGHT_RECORDER_DIR` selects the export root. The recorder is default-off,
allocates and writes nothing when disabled, and performs no disk I/O while
playing until F9 is pressed. The cost when armed is a native in-memory snapshot
every five seconds and storage for sixteen anchors; it should remain a
playtest/debug facility rather than a release default.

The companion `tools/minimize_route.py` performs transition-preserving ddmin
with repeated outcome checks and treats nondeterminism as an abort. Together
these tools turn a playtester's F9 bundle into a deterministic, shrinkable route.
**Serves:** every future playtest report.

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

### 12. Indirect-dispatch contract auditor and target logger
Use source-backed static extraction when the pointer writer is expressed in
the byte-exact disassembly, and retain the runtime logger for genuinely
dynamic/unknown writers. `tools/audit_animation_dispatch.py` now parses every
`%DKC1_AnS1_Op81(...)` use, resolves the callback symbols through the USA 1.0
Asar symbol file, and requires `$BE8179`'s cfg contract to be an exact,
canonical match. Runtime harvesting alone is not a completeness proof: the
first route-based contract covered only 16 of the 197 legal callbacks and
silently broke unvisited animations. The same static-first/runtime-second
rule applies to future dispatch gaps.

`tools/audit_indirect_tables.py` now applies that rule to every cfg contract.
Of 119 indirect dispatches, 118 are proven exact from byte-exact `DATA_*`
tables, explicit pointer-writer table links, or the 37 animation-record
callback fields. This includes all 108 normal-sprite main handlers and the
enemy, boss, barrel, rope, level, and section state tables. The only dispatch
outside that audit is `$BE8179`; its separate macro-based auditor proves all
503 uses/197 unique legal callbacks. Together the two reports leave zero
route-harvest-only indirect allowlists. `build/indirect-table-audit.json` and
`build/animation-dispatch-audit.json` are the current evidence.

Run both proofs and enforce the expected split with:

```powershell
tools\audit_dispatch_contracts.ps1
```

## Additional tools worth adding

These four are recomp-specific opportunities that were much harder to build
reliably through the emulator boundary:

### 13. CPU control-flow and stack integrity sentinel

Validate every RTS/RTL/RTI destination against the imported instruction map,
record the last 256 calls/returns, and stop on the *first* invalid stack frame.
This turns an eventual black screen into the exact producer instruction. It
would have caught the type-$05 retry's incorrect 16-bit push at the first bad
PLA rather than after execution returned into `$BD:FE01`. The downside is hot
CPU instrumentation, so it must be diagnostic-only and should use a compact
address bitmap rather than symbol lookup per instruction.

### 14. Tile/OAM boundary metamorphic fuzzer

Replay a deterministic frame while sweeping only host presentation width,
camera bias, and fine-scroll phase across `-1/0/+1`, 7/8, 15/16, 255/256,
and the two viewport endpoints. Gameplay WRAM must remain identical; center
pixels must remain the native oracle; only newly exposed margins may change.
This systematically finds the off-by-one guard-column and 9-bit OAM mistakes
that ordinary play reaches rarely. It cannot validate authored object timing,
so it supplements rather than replaces route tests.

### 15. Render/interaction correspondence oracle

For every visible object matched by source record and world position, compare
its rendered OAM bounds with conservative collision/pickup bounds and report
screen-space disagreement. This directly targets the historic “banana looks
right but pickup remains at the stock location” class. Collision formats vary
by actor family, so the tool must report `unsupported` instead of guessing
when a semantic adapter is absent.

### 16. Transition-state contamination bisector

At every source-signature, PPU-mode, or terrain-ready transition, retain the
last good and first bad trace records plus raw VRAM/OAM/shadow snapshots. Then
binary-search which reset/prefill/write first made a margin cell differ from
fresh-entry output. This is the quickest path for title/bonus/map transitions
that preserve stale side art. It depends on tools 1, 2, and 6 and should not be
built as a separate capture format—the bundle must reference their hashes.

Implemented as `tools/bisect_transition_contamination.py`. The headless host's
diagnostic zero-frame mode renders a loaded snapshot without advancing CPU,
APU, or PPU time, rebuilding only the host-side widescreen shadow. The tool
replays the original route to each sampled frame, saves its snapshot, performs
that clean-history render, and compares left/center/right pixels plus raw WRAM
and VRAM. It retains each sampled trace, snapshot, raw memory image, PPM, and
process log under one checksum-addressable report tree.

The fresh-entry sweep grader now also runs the strict transition analyzer for
every repeat. In addition to legacy terrain hit/miss and raw-fallback counts,
it rejects policy violations, nonblack centered margins, stable-input margin
changes, and unproven margin changes. This caught Snow Barrel Blast even when
the older tile-stat-only grader reported 40/40. Fixed-camera boss arenas are
an explicit safe-centered class only when the settled scene remains boss mode
and `camera.lower == camera.upper`; forcing those unaddressed maps wide was
visually tested and rejected.

Current accepted evidence:

- `build/fresh-entry-capability-floor-20260816/report.json` (current runtime,
  all 40 gameplay entrances retain widescreen capability; Slip-Slide Ride and
  Poison Pond independently select their cartridge-authentic decode mapping)
- `build/world-map-fresh-entry-sweep-v8/report.json`
- `build/world-map-fresh-entry-sweep-v8/grade.json` (40/40, 120 repeats)
- `build/snowbarrel-live-stream-capture-v6/summary.json`
- `build/imported-state-suite-live-stream-v20/manifest.json`
- `build/fresh-entry-stress-v6/report.json` (120 motion branches, zero hard
  presentation failures; all lifecycle differences retained as investigations)

Shared calibration, ROM-decoder, shadow, or presentation-policy changes must
also pass the committed capability-floor ratchet:

```powershell
python tools\check_widescreen_capability_floor.py `
  build\fresh-entry-capability-floor-20260816\report.json
```

This intentionally grades widescreen capability separately from route-level
gameplay investigations. An entrance may remain an investigation because a
neutral route does not prove gameplay closure while still passing the hard
requirement that it did not regress to centered 4:3, raw margins, missing
terrain, or nondeterministic presentation.

The stress tool replaces the old frame-to-frame OAM-slot wrap heuristic with
the evidence-grade dual-view oracle in `tools/oam_inspect.py`. DKC reuses OAM
indexes between unrelated objects, so slot continuity is not identity. A
real X-high loss now requires a PPU entry to match the current/recent WRAM
shadow in low X, Y, tile, and attributes while differing in bit 8; persistent
shadow/PPU mismatch is graded separately from the normal VBlank handoff.

`DKC1_WS_TRACE` also includes a full-WRAM hash. Stable-input margin grading
requires equal WRAM, VRAM, CGRAM, PPU OAM, WRAM OAM, PPU/register state,
camera/world coordinates, and widescreen identity, and excludes reset/source
boundaries. This caught and then verified the fix for the post-stream
13-pixel Jungle margin wipe without misclassifying changing decoded map data.

Example:

```powershell
python tools\bisect_transition_contamination.py `
  --runner build\dkc1_headless_tools.exe `
  --rom D:\private\DKC1_USA1.sfc `
  --snapshot build\snapshots\map-before-entry.state `
  --input-play build\routes\entry.inputs.txt `
  --good-frame 120 --bad-frame 180 `
  --output build\transition-bisect-entry
```

### 17. Arbitrary-snapshot widescreen stress matrix

`tools/snapshot_widescreen_stress.py` complements the fresh-entry sweep when a
tester supplies a native quicksave in the middle of a level or bonus room. It
never contacts the visible process. Every action/repeat starts a fresh process
from the same hashed snapshot, applies controller-only fixed/diagonal/sweep/box
patterns, and records the strict widescreen trace, rendered-blank detector,
lifecycle stream, dual OAM evidence, final raw WRAM, final frame, and v9 state.
Determinism includes the final machine, framebuffer, trace, lifecycle, and both
OAM artifacts—not merely whether the detector fired.

When a detector or accepted strict-grade failure occurs identically, the tool
replays that action, saves the exact trigger frame, emits a five-frame window,
and invokes `dkc1_layer_capture.exe` for same-frame isolated planes. Non-terrain
shadow misses do not trigger it; the strict grade must identify an actual
terrain/raw/policy/stability failure.

The Expresso Bonus matrix at `build/bonus-snapshot-stress-v3/report.json` ran
12 actions x 2 exact repeats for 420 frames. All machine and blank signatures
were deterministic. Across 7,992 extended gameplay frames it recorded
22,871,744 terrain hits, zero terrain misses, zero raw fallback, zero strict
failures, and zero blank-margin events. This proves the immutable state plus
those fixed/oscillating routes are clean; it does not replace the rolling
capture of the player's longer route.

## Suggested order against the current open issues

1 → 5 → 6 (attack the margin nondeterminism at frame 7,600 with evidence)
→ 3 → 4 (classify the wide-vs-native WRAM divergence and early activation)
→ 2 → 8 (make the object-fix routes provable) → 12 → 9 → 10 → 11.

Add tool 13 beside 12, tool 14 after the region-aware differ, tool 15 after
the lifecycle/OAM pair, and tool 16 once route snapshots and raw planes exist.

## Session results (2026-08-15, tool build-out)

Substrate and tools landed (commits b73d599, f14f769, a6376f7+):
script engine with wait/hold/pulse predicates and checkpoint/state
directives; evidence taps (WRAM hash log, input recorder, OAM dual log,
transition-only lifecycle trace with exact-frame sampling); wram_dump
ranged raw captures; first-divergence locator; OAM inspector; prefetch
phase auditor; regression contract runner (3x byte-identical gate);
macro minimizer; timeline exporter; level sweep grader; dispatch resolver.
Isolated tool build: build_host_tools.bat -> dkc1_headless_tools.exe.

First real evidence produced:

- **$BE8179 initially made executable** (open issue 7): the
  animation-callback `JML [$007A]` behind `PHK/PEA $810D`. The first
  `force_lle`/`DKC1_TRACE_PC` route harvest found 12 targets and removed the
  immediate unresolved-abandon reports, but route harvesting was later proven
  insufficient as a completeness method. See the static closure result below.
- **First stock-vs-wide divergence located and classified** (open issue 4):
  frame 7332 is byte-identical end to end; frame 7333 (first level frame)
  differs in 31 bytes — widened scanner window right ($0140 -> $0196),
  scanner record index 6 -> 8, two margin actors allocated (records 6/7,
  one at x=$0190 inside the right margin), bookmarks set. Divergence is
  entirely activation-window driven at entry; nothing outside the
  expected adapters fired earlier.
- **Prefetch audit over the Jungle route**: 59 episodes — 14 matched,
  34 indeterminate_without_stock_allocation (short route; margins activate
  records stock never reaches), 4 wide_persists_stock_culls
  (indeterminate — queue for the WRAM pass at the reported frames),
  1 behavior_phase_difference, 6 needing exact-frame samples.
- **WS trace step-0 capture** on the predicate route: zero raw fallbacks,
  zero margin-change-while-static events, zero prefill refreshes across
  7,645 frames. The margin-nondeterminism repro (issue 1) still needs the
  original fixed-frame 7,500-7,599 route and a gameplay-to-title
  transition capture under this trace.

Still open, deliberately left for the session owning dkc1_game.c:
two-phase calibrate/commit restructure (issue 2) and hard identity
invalidators (issue 3) — the trace now provides the evidence they need.

## Validation pass (2026-08-15, later)

- **Regression gate works end to end**: contracts/jungle-entry.json PASSES —
  all expects hold, 3 repeats byte-identical at both checkpoints.
- **Native baseline must be re-pinned**: the handoff's frame-7,600 native
  hash table was captured while $BE8179 callbacks were still being skipped.
  With the dispatch authorized, native runs are deterministic (two identical
  runs) at NEW values: frame dc629702..., WRAM 8a108fd6..., audio fnv1a
  5a54239ccb9cfcfe; CGRAM unchanged (2f6ce319...). This is a correctness fix
  changing the oracle, not adapter leakage — but the wide-vs-native
  inertness statement needs re-proving against the new baseline.
- **Transition margins are clean**: the callback-complete
  `recipes/route_death.dks` route now reaches the actual death/non-gameplay
  transition. `contracts/jungle-death-transition.json` passes two checkpoints
  across three byte-identical repeats. The trace contains 11,470 centered
  frames whose left and right margins all equal the exact all-black FNV-1a
  hash, with zero raw fallbacks and zero policy violations. Map/title, bonus,
  save-select, and cross-level transitions still need independent routes.
- **Two leads for the dkc1_game.c owner**: (1) WIDE<->CENTERED flapping at
  level entry (frames ~7304-7331) — the calibration flip-flop issue 2
  predicts; (2) the wide terrain world key unwraps to camX=$FFF0 (-16) on
  the first widened entry frame — check Dkc1VideoUnwrapPpuScroll around the
  zero boundary.
- wide_persists_stock_culls (4 records, +14..20 frames each) still needs
  the queued WRAM pass before any of them may be called benign.

## Save states (status)

Native full-machine snapshots exist at three layers: script directives
`state_save`/`state_load` (both hosts), env anchors DKC1_SAVESTATE_INPUT /
DKC1_SAVESTATE_OUTPUT / DKC1_SAVESTATE_SAVE_AT, and the F9 repro bundle's
embedded anchor. Loading resets the widescreen shadow by design
for legacy v4-v8 states; v9 restores the serialized host and hidden PPU state. Interactive
F11/F12 quick save/load is implemented in the desktop host.

Save format v9 serializes DKC1's host-only widescreen state and the PPU's
hidden VRAM/CGRAM/OAM data-port and latch state.
The world-keyed BG cache is stored sparsely (only valid cells, their
captured/prefill/served ownership, cooldown stamps, scene-local origin, and
vertical-history inputs), together with calibration identity, presentation
bias, stream-coverage state, and placed-actor phase decisions. Existing v4-v8
states remain readable through deterministic legacy reconciliation; new v9
states resume all of it exactly. `tools/verify_widescreen_savestate.py` proves this by
comparing an uninterrupted run with a fresh-process split-state continuation.
Its required oracle includes the final framebuffer, WRAM, VRAM, CGRAM, both
OAM copies, renderer state, and cumulative margin counters—not just a PNG.
When the flight recorder is armed, every interactive or scripted state load
also reanchors its rolling history at the current host-frame number.  Exports
after the load therefore contain only inputs applied to that loaded state.
With `DKC1_AUTO_EXPORT=1`, the visible host also enables the framebuffer
blank-margin detector without requiring a separate log path.  It distinguishes
centered presentation from proven extended gameplay, so a complete two-sided
gameplay cull now exports the rolling bundle rather than being mistaken for
intentional pillarboxing.
The same terrain-ready gate applies to cumulative shadow integrity counters:
transition teardown may update those diagnostics, but such updates are
consumed while terrain is unavailable and cannot cause an all-black fade to
be exported as a gameplay cull.
SuperZSNES v0.230 `.szst` states are now first-class repro inputs after
conversion through `tools/SuperZSNESStateExporter`. The exporter uses an exact
five-type deserialization allowlist, validates the 280,640-byte raw tail, and
emits a versioned `complete-source-state` bundle. Both hosts accept the bundle
directory through `DKC1_SUPERZSNES_STATE`. Imported state 5 has passed three
byte-identical three-frame native replays. Raw WRAM/VRAM/CGRAM/OAM/I/O in its
bundle also match an independently captured SuperZSNES frame byte-for-byte.
The importer intentionally reports `audio_history=reconstructed`: saved DSP
registers are restored but private interpolation history has no native field
equivalent, so audio is excluded from the first-frame cross-runtime oracle.

The visible host now has a reproducible Jungle snapshot library generated by
`recipes/capture_jungle_snapshots.dks`: map-before-entry, first valid wide
camera bounds, and stable gameplay. The local binary states live under
`build/snapshots/` and are intentionally not committed. Launch one without
replaying the boot route:

```powershell
tools\launch_visible_snapshot.ps1 gameplay -Trace
tools\launch_visible_snapshot.ps1 bounds -Trace
tools\launch_visible_snapshot.ps1 map -Trace
```

Additional route anchors can be generated from the stable-gameplay snapshot in
a separate visible window, without replaying the intro or disturbing an
existing interactive window:

```powershell
tools\capture_visible_snapshot_library.ps1
tools\validate_visible_snapshot_library.ps1
tools\launch_visible_snapshot.ps1 early
tools\launch_visible_snapshot.ps1 mid
tools\launch_visible_snapshot.ps1 late
tools\launch_visible_snapshot.ps1 route-end
```

The generated snapshots and `jungle-route-manifest.json` live under the
ignored `build/snapshots/` directory. The manifest records the immutable root,
route result, byte sizes, and SHA-256 identities so a later regression cannot
silently use the wrong anchor.

`recipes/snapshot_smoke.dks` is the two-frame visible-host load check.

The stable-gameplay anchor also removed boot timing from a new visible
stock/wide comparison. Frame 1 was byte-identical. At frame 2 the widened
scanner advanced from record `$0C` to `$0D`, and actor index `$06` (ID `$05`,
Kritter, source record `$0C`) changed only its last-rendered pose cache `$0AE5`
from `$1E18` to the already-identical desired pose `$1E1C`. ID, source, world
position, velocity, state, animation and desired pose were identical. Stock
caught up after the actor entered its native render window; both modes had
reconverged on the same pose by frame 10. The first ten compared frames contain
no actor-gameplay, bookmark, section, entrance, fade or logical-camera
difference. `tools/first_divergence.py` now reports this conditional
`render_pose_refresh_only` case and WRAM OAM-shadow deltas as presentation,
while retaining all raw and unclassified scratch ranges. This avoids promoting
an earlier draw-cache refresh into a false behavior-phase bug.

## Follow-through pass (2026-08-15, latest)

- **Persists records classified from raw WRAM** (tools/analyze_persists.py):
  all three authored wide_persists_stock_culls records grade
  release_delayed_by_wider_window -- constant state/anim across every
  extension frame, drifting off-screen left at walk speed, freed at the
  widened despawn threshold (the 14-20 extra frames equal the extra ~56px
  at ~3px/frame). The fourth was a source-0 (non-authored backlink)
  alignment artifact; the auditor now refuses to align source <= 0.
- **Adapter inertness re-proven against the new baseline**: a generated
  tree built WITHOUT apply_dkc1_widescreen_overrides.py
  (build_host_noadapt.bat -> dkc1_headless_noadapt.exe) produces
  byte-identical native frame/WRAM/VRAM/OAM hashes and audio FNV to the
  adapters-applied build at frame 7,600. The re-pinned baseline is backed
  by a real no-adapter oracle, not just determinism.
- **$BE8179 statically closed at 197 targets.** A deterministic one-frame
  jump trace first recovered missing callback `$BEA778`, the jump animation's
  final callback that switches to idle/ground movement. Omitting it made
  `Op80` restart the jump script and reapply the `$0700` Y-speed impulse every
  40 frames after the key was released, even though every controller mirror
  was zero. That exposed the deeper defect: the disassembly contains 503
  `%DKC1_AnS1_Op81(...)` uses naming 197 unique callbacks, all 197 resolve in
  `DKC1_U1.sym`, while the route-harvested cfg listed only 16. The cfg now
  contains the exact canonical 197-target set. The generated program has
  2,691 exact AOT variants and zero LLE-only variants. The 240-frame regression
  applies one jump input, records exactly one `$0700` impulse at frame 7586,
  and observes animation `$10D3` return from jump `$0005` to ground movement
  `$0001` at frame 7625. `build/animation-dispatch-audit.json` records 503
  calls, 197 expected/actual targets, zero missing/extra/unresolved targets.
- **Every recomp indirect dispatch now has a source-backed closure proof.**
  `tools/audit_indirect_tables.py` matches 118 of 119 cfg contracts against
  explicit disassembly tables/records with zero missing or extra targets; the
  remaining `$BE8179` contract is the separately proven 197-target animation
  callback set. This removes route coverage as a prerequisite for executing
  an unvisited actor or state handler.
- **F11/F12 quick save/load** added to the desktop host. F11 writes the native
  machine to `quicksave.state`; when the rolling recorder is armed it also
  exports the covered anchor, resolved input history, final raw machine
  planes, and same-frame isolated layer captures. This matters because the
  v9 state, including the host-only widescreen shadow, placed-actor phase
  history, and hidden PPU port/latch state, so a margin-history or post-load
  character-DMA bug remains reproducible after F12. The
  default-off `DKC1_WS_COLD_STATE_LOAD=1` diagnostic discards only that host
  history when an exact cold reconstruction is required.
- **Contact damage/death is closed.** The earlier no-damage result belonged
  to the incomplete animation-callback build. With all 197 legal `$BE8179`
  targets present, `recipes/route_death.dks` clears both Kong actor slots and
  reaches the expected non-gameplay transition. The three-repeat closure is
  `contracts/jungle-death-transition.json`; trace evidence is under
  `build/full-contract-death-wide/`.
- **Stable-input margin diagnostics are now strict.** Frame hashes include
  CGRAM as well as VRAM and both OAM copies. The analyzer also requires equal
  selected PPU state, scrolls, camera/bounds/bias, world keys, and native
  center pixels before calling a margin-only change nondeterministic. A fresh
  932-frame snapshot-scroll trace (`build/current-cgram-trace/summary.json`)
  reports zero stable-input margin changes, zero unproven changes, zero raw
  fallbacks, and zero centered nonblack margins. Older alerts that omitted
  palette/scroll/camera state were false positives and must not drive a patch.
- **Both transition directions are trace-gated.** `contracts/jungle-entry.json`
  covers the 7,645-frame fixed-screen/map-to-gameplay route (315 extended,
  7,330 centered frames); `contracts/jungle-death-transition.json` covers the
  11,972-frame gameplay-to-nongameplay route (502 extended, 11,470 centered).
  Each passes three byte-identical WRAM/VRAM/OAM checkpoints and byte-identical
  complete trace files. Both have zero raw fallbacks, policy violations,
  nonblack centered margins, or stable-input margin changes.

## Lean reverse-watch function windows (2026-08-16)

`build_host_trace.bat` builds the default-off `SNESRECOMP_WATCH` path and now
runs `tests/lean_watch_attribution_model.c` as a mandatory boundary-model
regression. The model covers a parent write before a nested call, a child
write, a parent write after the child returns, and a write made outside any
generated function. The expected writers are parent, child, parent, and
`host/outside-function-window`; the old entry-only sampler incorrectly labeled
the third and fourth cases with the stale child/previous entry. It also pins
the concrete `$0028`/`CODE_80C0F8_M0X0` stale-entry regression and verifies
that a watchdog-abandoned owner cannot claim a later host-boundary sample.

The concrete `$0028:2` regression was also replayed headlessly twice across the
full 11,972-frame `recipes/route_death.dks` contract. The repaired logs are
byte-identical (`SHA-256 2A31065778C04AAD50DC7ACEFCDA7CCEA8D540254B889E30CFA48E2A22FC9D79`)
and each contains 11,599 byte changes, zero rows attributed to the joypad-only
`CODE_80C0F8_M0X0`, and no truncation markers. 11,595 changes were correctly
left unattributed because DKC's top-level interpreter tier made them outside
an AOT function window; the other four were AOT-window resets. This is the
expected fail-closed result, not evidence that the host itself wrote `$0028`.
With the watch environment unset, the hook build and a freshly built no-hook
control also produced identical final frame, WRAM, VRAM, CGRAM, OAM, source
OAM, and audio hashes on the same route.

The lean contract is deliberately function-window resolution, not a claim of
instruction-exact stores. A watched byte is sampled at every matched generated
entry and exit. The JSONL row carries `attributed`, `attribution`, `writer_pc`,
`writer`, and the observing `boundary`. Net changes within a window are
attributed to that function's entry PC. Multiple writes that restore the byte
before the next boundary are not visible; use force-LLE plus `DKC1_TRACE_PC`
when the exact opcode or an intra-function transient matters. Changes first
seen with no AOT window active remain explicitly unattributed so interpreter,
host, or state-load work can never implicate the last generated guest routine
by proximity. If a watchdog abandons a generated window, the next host boundary
also clears its owner before sampling because host and pre-unwind writes can no
longer be separated safely.

The parser accepts at most 16 nonoverlapping WRAM ranges and 4 KiB total. Zero
lengths, malformed hex, overlaps, range overflow, and out-of-WRAM endpoints
disable the watch instead of partially arming it. A 256-row per-frame safety
limit emits `watch_truncated`; `tools/reverse_watch.py` treats that marker as a
failed evidence run and asks for narrower ranges.

## Aquatic presentation candidate (2026-09-06)

The independently default-off `DKC1_WS_PIXEL_BOUNDARIES=1`,
`DKC1_WS_LIVE_SCROLL=1`, and `DKC1_WS_WALL_ADJACENCY=1` backports are documented
in [the aquatic validation record](WIDESCREEN_AQUATIC_BACKPORTS.md). The trace
records root `presentation_features` bits 1/2/4 and
`boundary_adjacency_tiles` (also part of `boundary_continuation_tiles`).
Headless/layer capture accept `DKC1_ASPECT=16:10` with widescreen enabled.
Native-edge and scanline-scroll evidence passes for the preserved aquatic
branches; wall continuation is model-tested only. The complete 40-entrance
floor remains unpassed because 36 required clean anchors are unavailable.

## September 6 Mac host and cache-boundary additions

- `DKC1_WS_SCROLL_REBASE=1`: default-off, currently calibrated cache rebuild on the exhausted-window frame. WS trace feature bit 8 and `decision.cache_rebase` identify it. `verify_shadow_localization.py` requires a calibrated cold commit for any marked origin change. See `docs/WIDESCREEN_WATER_FLASH.md` and `recipes/croctopus-cache-crossing.json`.
- Native **Game → Controls and Assist…** exposes source routing, remapping, analog deadzones, and opt-in rewind/3× fast-forward. Default Assist holds: Backspace/Tab or left/right triggers. Game time remains canonical. Rewind memory is capped at 128 MiB; actual history duration depends on serialized-state capacity. See `docs/HOST_ADOPTION_IMPLEMENTATION.md`.
- `DKC1_ASSIST_TEST_INPUT=<input file>`: default-off Mac-only host-action schedule using the existing hex/repetition format; masks 1=rewind, 2=fast-forward, 4=quick-save, 8=quick-load. It enables Assist only for that run. Save/load actions operate the normal quicksave path; do not use those bits against a tester's only state. `DKC1_ASSIST_TEST_LOG=<path>` optionally records host tick, host/guest frame, pops, and history depth. Gameplay playback stays independent and is abandoned after a rewind/load.
- `DKC1_PAUSE_AFTER_FRAME=<positive host frame>`: default-off Mac exact-frame pause. Clears gameplay and host-action schedules and stops sound/rumble; preserves the actual submitted pixels for window capture. `DKC1_SAVESTATE_OUTPUT` also works at graceful Mac shutdown. Use private paths, not the user's normal slots.
- `DKC1_PACING_LOG` adds `audio_ratio`, `audio_fill_average`, and `audio_target_frames`. Canonical production is unchanged; only mixed host PCM is resampled. `DKC1_SCANOUT_LOG` repeat goal 0 means unqualified/non-integer cadence using target timestamps; goals 1–4 are qualified divisors.
- Flight bundles and post-failure input tails preserve both controllers as six-digit masks. The verifier accepts both historical three-digit and new six-digit masks. Existing bundle schema and hashes remain valid.

## September 18 Windows pose and frame generation audit

Windows release packaging: after committing and running `build_host.bat`,
`python scripts/package_windows.py --output build/release-VERSION` packages
the native host, player instructions, release notes and license notices.
The clean embedded commit must match HEAD. `BUILD.json` and the ZIP SHA-256
sidecar bind the exact files; extraction and a real launch are separate
release verification steps. The package file list never includes ROMs,
private states, generated game sources or diagnostic captures.

The default-off Windows smoother now buffers four frames to interpolate held
OAM artwork at 60 Hz and fractional pose/motion phases at 120 Hz. A separate
immutable-image midpoint presenter leaves the producer a full 16.67 ms budget.
No gameplay or widescreen policy change is involved. Unsupported composition
and ambiguous overlapping groups retain raw artwork.

`tools/verify_framegen.py` is the serial visible-host A/B/repeat/timing gate;
its exact arguments and environment taps are in `.claude/skills/dkc1-tools/TOOLS.md`.
Raw `_prev/_cur.ppm` images remain unmodified. `_display.ppm` is delayed F;
`_mid.ppm` is the following F+0.5, identified by `pose_source_frame` in JSON.
`DKC1_POSE_LOG` records artwork tracks; `pose_mismatch` records a failed
per-surface composite oracle. Dumping is not a cadence test. The independent
undumped `dkc1.pacing.v4` trace includes actual real-to-mid and mid-to-real
software submission intervals and the midpoint's `mid_after_frame` identity.

See `docs/FRAMEGEN_REVIEW.md` for frame-by-frame evidence, root/build/ROM hashes,
validated scope and remaining hardware/scene coverage. `force` mode is only
software-path proof on this machine's 60 Hz display. Animation-cadence logging
continues to describe original cartridge poses, not the generated output.

The follow-up running audit in `docs/BACKGROUND_PACING_REVIEW.md` keeps spatial
judder separate from timing outliers. `tools/analyze_bg_frame_steps.py` seeds
visible textured pixels from independent same-frame BG captures, follows
exact correspondences through delayed real/mid images, and rejects ambiguous
or insufficient texture. `--cadence 60|120` selects real-only or real/mid
phases; JSON records signed pixel steps, matched-point counts and confidence.
It is read-only and makes no scanout claim. Undumped timing remains a separate
`DKC1_PACING_LOG` run; clean submission averages cannot prove smooth layers.

The opt-in fractional BG stage and its exact/fresh running evidence are in
`docs/BACKGROUND_SMOOTHING_REVIEW.md`. `DKC1_FRAMEGEN_BG=0` disables the stage;
`DKC1_FRAMEGEN_BG_SYNC=1` selects the serial implementation for worker A/B.
`DKC1_BG_MOTION_LOG=<path>` records source phase, processed pixels, extraction
oracle failures and row-112 layer offsets. This log is not visual proof.
`analyze_bg_frame_steps.py --fractional` independently fits actual displayed
RGB to native layer references at 1/16-pixel resolution, rejecting ambiguous
texture. `verify_framegen.py --capture-window` adds actual-window evidence only
to dumped runs; its helper uses per-monitor DPI coordinates. The harness now
distinguishes `frames_with_generated_poses` (nonzero actor count) from
`frames_with_generated_pixels` (any modified pixels, including backgrounds).
Undumped 60 Hz DXGI timing remains independent of this spatial proof.

Windows pacing follow-up: `tools/verify_pacing_soak.py` runs serial continuous
routes (default: two 108,300-frame repeats in each of windowed/fullscreen).
Use `--exe`, `--rom`, `--state`, `--input`, and a new `--output` directory;
`--queue 1|2` selects `DKC1_MAX_FRAME_LATENCY`. With `--trace-directory`, the
trace-only elevated `tools/pacing_trace_helper.ps1 -EvidenceDirectory DIR`
must already be ready and `DIR/PresentMon.exe` must be provisioned. PresentMon
API-only timestamps are correlated per PID/frame by `analyze_presentmon.py`;
they do not substitute for DXGI scanout counts. Missing data fails closed.
`--kernel --kernel-phase audio_ms --kernel-trigger-ms 10` is a diagnostic
capture: stop bounded WPR history at the first qualifying steady hitch, while
the game finishes its route normally. This path remains unvalidated: the
September 18 WPR stop failed with `0xc5580612`. It is never an acceptance run. Logs use
a bounded asynchronous writer and require a clean `.status.json` sidecar.
See [the pacing investigation](PACING_HARDENING_REVIEW.md) for trace limitations.

## Mac graphics and pause-menu diagnostics (September 6, 2026)

The Metal presenter now supports DKC2 Reconstruct/CRT and four phosphor profiles. Raw WS/plane/state evidence is collected before the color-copy and shader stages. Use Flat + Raw + Nearest for visible native-pixel comparison; other selected effects intentionally change visible RGB. All guest timing and tile-streaming diagnostics remain independent.

`test_macos_graphics` provides standalone GPU checks for native transfer, all modes/presets, cached repeats, and invalidation by changed pixels/settings/viewport. Build with `cmake --build build/macos --target test_macos_graphics` and run `build/macos/test_macos_graphics runner/macos_graphics.metal [existing-output-dir] [binary-P6-input]`. Optional test-only geometry: `DKC1_TEST_SCALE=1..16` and `DKC1_TEST_PIXEL_ASPECT=1`. It returns 77 when no Metal device is available.

For actual-window QA, combine a separate application bundle identifier with `DKC1_USER_DIR=<absolute existing directory>` to isolate preferences and saves respectively. Startup options are `DKC1_DISPLAY`, `DKC1_UPSCALER`, `DKC1_SCREEN`, `DKC1_CRT_PRESET`, and `DKC1_RECONSTRUCT_MODE/STRENGTH/SOFTNESS/SHADING`; the canonical values are cataloged in `.claude/skills/dkc1-tools/TOOLS.md`. Omitted variables use saved `GraphicsV1` settings. Do not set diagnostic input, snapshot, or output paths in a normal app build. See [graphics port evidence and limits](GRAPHICS_OPTIONS_PORT.md).

## Coral Capers populated wall junction (September 6, 2026)

The default-off `DKC1_WS_WALL_SEAMS=1` capability repairs both verified faces of one offscreen junction, with independent source and native-edge containment for each direction. It exposes trace feature bit 16 and optional count `wall_seam_tiles` (trace schema maximum feature mask is now 31). Diagnostics remain inert when disabled. Use raw BG1 plus composite and separate native-center hashes; the defect exists before graphics postprocessing. Exact-state and scrolling acceptance, donor-map proof, and the unfulfilled same-level fresh-entry gate are recorded in [WIDESCREEN_WALL_SEAM.md](WIDESCREEN_WALL_SEAM.md). Replay with the switch explicitly enabled; do not infer its value from the state file.

## Isolated HD scene presentation experiment

The HD fork adds default-off 4× BG and OBJ material replacement after an exact
native-composition check. The master switch is `DKC1_HD_SPRITES`; the full-scene
path uses `DKC1_HD_SCENE`, `DKC1_HD_SCENE_PACK`, and `DKC1_HD_SCENE_EXPORT`.
`DKC1_HD_SCENE_AUDIT` verifies the reconstructed native result each eligible
frame. `DKC1_HD_SCENE_TRACE` reports per-presentation mismatch pixels and HD
material coverage. Missing art or unsupported composition stays original.

The trace also includes `cache_entries` (occupied slots, maximum 4096),
`cache_evictions` (cumulative replacements), and `cache_failures` (cumulative
refused native-raster lookups/allocations). A missing HD file is a material
miss, not a cache failure. These counters never write guest memory.

`tools/build_hd_preload_manifest.py PACK` validates all DKHD file headers,
dimensions and lengths and atomically writes the sorted `preload.txt` index.
The default-off `DKC1_HD_SCENE_PRELOAD=1` option reads every indexed raster into
owned CPU/shared memory before the first supported scene frame and retains it
through scene transitions and material-cache eviction. An invalid/incomplete
index fails closed to original pixels with a diagnostic. Missing art never
causes a texture read after preload. Trace fields `resident_materials`,
`resident_bytes`, and `material_file_reads` prove residency and the invariant
that cumulative DKHD open attempts remain constant during gameplay. This does
not change guest memory, the original-pixel oracle, supported scenes, or the
composition algorithm; a Metal compositor remains separate work.

`DKC1_HD_FRAME_PPM` exports the final HD image. `DKC1_HD_SEQUENCE=1` selects HD
for the existing frame-sequence exporter; `DKC1_HD_RENDER_EVERY_FRAME=1` composes
every frame for coverage/performance checks. The original native frame and
raw guest taps remain unchanged. Workers only read immutable completed data.

The private source atlas/upscale/pack pipeline, full command contracts, exact
state/fresh-entry results, screenshots, and bounded 16:9 art scope are in
[HD_SPRITE_EXPERIMENT.md](HD_SPRITE_EXPERIMENT.md) and the tools catalog.

The separate Magnific Precision V2 experiment uses
`tools/extract_jungle_sprite_inventory.py` (checksum-locked family extraction)
and `tools/magnific_precision_experiment.py` (verified native atlas preparation,
fixed 4× crop/alpha assembly, exact facing registration, and pose viewer).
Cloud submission remains an explicit MCP operation with cost simulation and
recorded creation IDs. The registered packer permits fully opaque art only for
verified opaque originals, such as a one-pixel effect. The normal renderer and
widescreen policy are unchanged. See the canonical tools catalog for command
contracts and [MAGNIFIC_PRECISION_V2_EXPERIMENT.md](MAGNIFIC_PRECISION_V2_EXPERIMENT.md)
for the private scope, coverage, provenance, limitations, and replay evidence.
The companion `magnific_scene_materials.py` verifies captured background contexts
and assembles exact crops with optional source-colored boundary feathering.
`magnific_banana_hud.py` decodes checksum-locked primitives and composes all 800
normal count/animation combinations. Both are offline asset tools; missing
runtime context/overlap keys still fail closed to original pixels.

`tools/check_hd_sprite_alignment.py` separately checks generated replacement art
against its source canvas, silhouette, bounds, and manually identified landmarks.
It does not modify or automatically center either image, and a matching outline
cannot substitute for eye/muzzle/hand/foot registration or visual art review.

`tools/verify_hd_scene.py ROM OUTPUT --pack CANDIDATE_DIRECTORY` selects a private
candidate for the existing 36-replay matrix without replacing the baseline
pack. Enabled runs compose every frame, including walking frames before an idle
endpoint. The report records pack path/content hash and guest/HD determinism;
artwork alignment and visible animation require their own review.

The September 6 animation follow-up adds explicit `--cases` selection for
`directions`, `actions`, `run`, `hurt`, `hurt-left`, and `idle-extended`, alongside
the existing cases. `--export-materials` preserves exact encountered object
rasters on the first enabled repeat. The default remains 36 replays; each
selected case runs twelve comparison legs. See the tool catalog for scope.

The barrel continuation adds `barrel-right` (500 frames) and `barrel-left`
(940 frames): controller-only pickup, carrying, turning and throwing from
both directions, followed by recovery. Both use the same immutable private
Jungle root. They preserve the same twelve-leg guest/HD determinism contract.
`barrel-jump` adds an 870-frame route with carrying jumps in both directions.

The bounce/idle continuation adds `idle-cycle-right` (600 frames),
`idle-cycle-left` (660), and `bounce-right`/`bounce-left` (340 each). These
cases automatically export exact raster keys and require all registered
21 Idle + 24 BeatChest poses, or 16 Bounce poses, in the route's direction.
The `ground-slap-right` (330 frames) and `ground-slap-left` (436) cases
require all 30 GroundSlap and 22 Duck poses in the appropriate direction,
including displayed-raster coverage. They hold Down+Y for 180 frames and
release for 120; the left case moves clear of the cave before turning.
`slap-transitions-right` (787) and `slap-transitions-left` (893) additionally
exercise button order and release transitions. All four require every route
frame eligible for HD audit, zero reconstruction mismatch, and installed HD
materials for every encountered DK raster, including other animation groups.
`--dk-originals` selects the complete private original corpus; Pillow is
required for its raster index. `--state` supplies an immutable tester root
for non-fresh cases. The routes retain HD off/on, native/wide, three-repeat
comparison. See [HD_GROUND_SLAP_REVIEW.md](HD_GROUND_SLAP_REVIEW.md).
Missing pack entries and skipped required poses fail even if guest hashes are
deterministic. The original twelve-leg comparison contract still applies.

`cache-pressure` runs 3,347 frames: controller traversal fills the real
4096-entry material cache, a normal scripted load restores the private
immutable entry, and 966 subsequent frames exercise idle and both directions.
The case requires actual eviction, zero cache failures, and exact native
reconstruction throughout those 966 frames. It retains the twelve-leg HD
on/off, native/wide, three-repeat comparison. Earlier traversal mismatches
are reported separately and are not silently treated as fixed.
See [HD_MATERIAL_CACHE_REVIEW.md](HD_MATERIAL_CACHE_REVIEW.md).

`tools/hd_sprite_pack.py registered` verifies original content hashes and builds
both facing keys from the same reviewed candidate. This fixes stale or missing
left-facing materials without changing the compositor or any guest code. The
manifest records both keys per frame. Private packs, current validation and the
700-frame contact-sheet review are documented in `HD_ANIMATION_REVIEW.md`.

### HD Metal composition oracle (September 16, 2026)

The default-off `DKC1_HD_METAL=1` Mac preview path uploads the preloaded pack once,
captures immutable frame packets, computes the native-oracle mask and HD image
on the GPU, and sends the resulting texture directly to the Metal presenter.
It preserves the existing scene capability and guest behavior. Raw screen color
mode is supported; other screen color models retain CPU composition.

`verify_hd_scene.py ROM OUTPUT --pack PACK --preload --metal --cases fresh run
cache-pressure` adds an every-eligible-frame CPU/GPU image comparison to the
existing native/wide, HD off/on, three-repeat contract. Metal validation failures
exit 22. `DKC1_HD_METAL_VALIDATE=1` is a headless-only diagnostic; never time it
as the production GPU path. `DKC1_HD_METAL_SHADER` points to the shader with its
sibling ABI header. `DKC1_HD_METAL_TRACE` records GPU errors/timing and validation
pixel counts. Scanout traces include `hd_gpu`; CPU scene trace counters are only
emitted by the CPU compositor. All new diagnostics remain default-off.

Synthetic tests in `test_hd_metal.py` exercise actual Metal output and packet
ownership with ASan/UBSan, without ROM data. The Metal graphics harness checks
resident-texture/CPU-upload parity across all display filters and inset
viewports. See [HD_METAL_COMPOSITOR_REVIEW.md](HD_METAL_COMPOSITOR_REVIEW.md).

### Connected Nano coverage, contours and matte removal

The private opt-in `DKC1_HD_EXACT_CENTERS` / `DKC1_HD_CONNECTED_WORLD` path binds
connected art only to verified native source tiles and the documented Jungle
scene tuple. It follows PPU ring position, validates each 8x8 tile, and handles
palette animation independently of source identity. Unsupported scenes still
fall back. No WRAM/VRAM/OAM writes are introduced. Preload also loads
`object-silhouettes.txt` when present: exact native object mask/size aliases
reuse registered OBJ art after live CGRAM color changes. Optional
`object-bases.bin` supplies the authored native colors so night palettes still
tint that art. Background center aliases stay isolated. Malformed object
indices or base blobs fail closed. The resident pack loads on the first
prepare, and INIDISP 1-14 keeps HD instead of popping back to original pixels.
A prevent-math CGWSEL value does not reject the scene when CGADSUB is 0.

`DKC1_HD_COVERAGE_TRACE` reports per-layer source and visible missing counts,
scene eligibility, camera and native reconstruction mismatches. The visible
metric uses the top native opaque layer, not HD alpha coverage. The diagnostic
is costly and default-off. `DKC1_HD_MISSING_EXPORT` writes exact source and
canonical-context PAM/JSON; the first `DKC1_HD_SCENE_EXPORT` atlas also includes
raw source WRAM, VRAM and CGRAM. A canonical object candidate alone is not proof
of correct palette interpretation.

`DKC1_HD_POLISH=0..100` controls the optional spatial Metal cleanup. Raw CPU/GPU
agreement remains a separate oracle, even while intentional postprocessing is
active. OBJ/HUD bounds and all original/mismatch fallback are protected. The
native graphics menu exposes Edge cleanup; zero means raw HD, and F10 remains
original/HD comparison. Normal defaults stay off.

`audit_hd_jungle_map.py`, `build_hd_connected_pack.py`, and
`build_hd_stream_boundaries.py` cover the source audit and private connected
asset build. `hd_sprite_matte.py` creates a new pack from verified registrations,
unmixes sheet matte near source edges, protects real gray paint, and preserves
canvas/anchor and bounded silhouette geometry. Never overwrite a running pack.
Their command/format contracts are in `.claude/skills/dkc1-tools/TOOLS.md`.
See [HD_NANO_POLISH_REVIEW.md](HD_NANO_POLISH_REVIEW.md) for acceptance scope,
remaining missing sprite poses, and the difference between raw determinism,
replacement coverage, and visual quality.

### Connected HD scrolling stability (September 16, 2026)

A mixed 32x32 streaming chunk now retains its verified connected-world art.
Only invalid 8x8 subtiles use the independently byte-exact fallback material.
Both references stay pinned across cache eviction; the immutable Metal packet
includes `bg_fallback_material[3][256]`. Deploy the matching shader and ABI
header with the executable. Palette correction and the original reconstruction
oracle retain their existing rules. This does not widen cartridge streaming.

`verify_hd_scene.py --cases replay --state STATE --input-play INPUTS --frames N`
repeats arbitrary recorded input from an immutable root in native/wide and HD
off/on modes. Input existence and a positive frame count are required. Input
SHA-256 and count are included in `results.json`. Add `--coverage` to retain
visible missing BG/OBJ samples, supported-frame count and missing-BG frame
count. Coverage is reported separately from correctness and is not an automatic
zero-miss gate. `--preload --metal` retains the every-frame raw CPU/GPU oracle
and zero-gameplay-read checks. See [HD_NANO_POPIN_REVIEW.md](HD_NANO_POPIN_REVIEW.md)
for exact-state and fresh-entry evidence and the remaining art-coverage limits.

The partial-left connected BG0 cell is bound using the visible-left ring period,
not the native-camera period. Unit coverage includes a wide left edge straddling
512px, right wrap, and negative-world rejection. Exact-state A/B, source planes
and native Metal QA: [HD_NANO_LEFT_EDGE_REVIEW.md](HD_NANO_LEFT_EDGE_REVIEW.md).

### Jungle Bonus 1 HD capability (September 16, 2026)

The opt-in HD renderer supports the verified Jungle Bonus 1 cave tuple with an
independent resident `connected-cave.bin` and source palette. It never changes
cartridge streaming or global widescreen capabilities. Color-window mode 1 is
accepted only for uniform empty spans, respecting inversion; scanline changes
that require spatial masks reject HD for the frame. OBJ palette math exemption
is carried in the immutable GPU packet. Source, state, scene transitions and
visible native QA are documented in [HD_NANO_BONUS_REVIEW.md](HD_NANO_BONUS_REVIEW.md).

`build_hd_object_composites.py --pack PACK --captures DIR... --libraries DIR...`
assembles only exact, fully covered object combinations from installed Nano
primitives and writes `object-composite-provenance.json`. It skips existing
materials and rejects uncovered pixels. Rebuild the preload manifest afterward.
Coverage records use the actual entrance; guard rejection adds `eligible_scene`,
`bgmode`, `inidisp`, `mosaic`, `window_main`, `cgwsel`, and `cgadsub`. Verifier
`pack_indices` hashes the optional world/center/preload indices separately.
