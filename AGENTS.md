# DKC1Recomp Project Rules

These instructions apply to every change in this repository. They are the
default operating rules for Codex and other coding agents working on DKC1Recomp.

## Project objective

Produce a playable, deterministic native recompilation of DKC1 with optional
widescreen presentation while preserving the cartridge game's behavior and the
native 256x224 image. A visually wider frame is not a valid result if it damages
the native image, object lifecycle, collision, exits, bosses, timing, or save
state continuity.

## Required widescreen model

Treat every widescreen issue as three independent domains until evidence proves
otherwise:

1. **Presentation:** host framebuffer, crop, margins, layer composition, masks.
2. **Streaming and activation:** VRAM tile rows/columns, ring buffers, OAM,
   object scanner windows, allocation, lifecycle.
3. **Gameplay logic:** collision, pickups, exits, movement limits, boss arenas,
   scripted state machines.

Do not repair one domain by silently changing another. A visual-only correction
is incomplete when interaction or gameplay coordinates still disagree.

## Non-negotiable defaults

- Stock cartridge behavior is the default for every unproven scene and layout.
- Widen presentation globally only where the host can do so without mutating
  cartridge state.
- Widen cartridge streaming, activation, or logic only after that exact
  subsystem and layout have passed the promotion gates below.
- Unsupported presentation states fail closed to stock output or black margins.
  Never repeat, wrap, or invent unverified side art.
- The original 256-pixel center is an oracle. It must remain pixel-exact unless
  a deliberate center correction is separately documented and approved.
- Keep the experimental cartridge initializer and row-stream widening disabled
  by default. `DKC1_ENABLE_EXPERIMENTAL_CARTRIDGE_WIDENING=1` is a research
  switch, not a release configuration.
- Reaching a shared DKC initializer, streamer, or renderer is not proof that a
  particular room or tilemap supports widened cartridge writes.
- Prefer explicit, evidence-backed capabilities over scene denylists. A scene
  tuple may be used as a narrow containment guard, but it is not the long-term
  architecture.

## Authoritative source and decompile library

Use `reference/` for all decompile, disassembly, IDA, pseudocode, RAM-map, and
historical widescreen research. Check it before searching external paths or
starting a new decompilation.

- `reference/disassembly/` is the primary byte-exact DKC1 source oracle. It
  contains the labeled disassembly, RAM and define maps, mechanical pseudocode,
  lossless listing, instruction index, seeded IDA database, curated rename map,
  reverse-engineering notes, and byte-identical assembly inputs.
- `reference/legacy-widescreen/` is historical evidence from the retired
  emulator/asar effort. Use its worklogs and tools to recognize bug classes,
  but never treat its patched ROM behavior as stock or copy its ROM-patch
  architecture into the recomp without new evidence.
- `reference/dkc-recomp-seed/` is the older bring-up seed. Use it for historical
  comparison only.
- Start an address or symbol investigation with
  `python tools/atlas.py <address|wram:address|name:term>`. The atlas joins the
  reference disassembly, pseudocode, IDA annotations, recomp variant, dispatch
  contracts, WRAM labels, and known issues.
- Ground every symbolic claim in the clean-ROM bytes or byte-exact reference.
  Names and decompiler output are navigation aids, not proof of semantics.
- Treat `reference/` as read-only. Most of its contents are intentionally
  ignored because they are large, generated, externally versioned, or contain
  build inputs that must not be committed. Fixes land in `runner/`, `recomp/`,
  `tools/`, `recipes/`, `contracts/`, or documentation as appropriate.
- Never hand-edit `generated/`. Recompilation-correctness fixes belong in
  `recomp/*.cfg` followed by regeneration; presentation fixes belong in the
  host/adapters or the pinned engine when explicitly scoped.

Read `reference/README.md` before moving, regenerating, or updating any source
oracle. Preserve the nested disassembly repository and its history.

## Before changing widescreen code

1. Read `docs/WIDESCREEN.md`, `docs/WIDESCREEN_HANDOFF.md`,
   `docs/WIDESCREEN_DEBUG_TOOLS.md`, and `docs/KNOWN_ISSUES.json` for the
   affected subsystem.
2. Record the clean ROM hash, executable/build identity, state hash, level,
   entrance, mode, aspect ratio, frame, and exact input schedule.
3. Preserve supplied save states outside the normal slot directory. Never
   overwrite the only tester reproduction.
4. Capture both the final visible window and raw WRAM/VRAM/CGRAM/OAM/PPU or
   isolated BG/OBJ evidence. An internal render target is not the sole visual
   oracle.
5. Establish one-variable stock-versus-candidate A/B evidence before editing.
6. Use one live automation owner at a time. Do not race controller, bridge, or
   save-state operations from multiple processes or agents.

## Save-state rule

Every state-based visual or lifecycle report requires two branches:

- **Exact-state branch:** reproduce and diagnose the machine state the tester
  supplied.
- **Fresh-entry branch:** enter the same scene from an earlier clean immutable
  state on the candidate build.

A save state may contain already-corrupt VRAM, OAM, margin history, or object
bookkeeping. If fresh entry is correct while the supplied state remains broken,
report the state as historical evidence. Do not add a broad repair pass merely
to rewrite old serialized corruption.

## Change design rules

- Change one subsystem at a time. Do not combine initializer, row-builder,
  object-window, gameplay-bound, and presentation experiments.
- Give risky experiments a default-off feature flag and a clean rollback path.
- Use the smallest coordinate-domain correction that makes rendering and
  gameplay agree.
- Preserve stock logical bounds when camera bounds are widened unless the
  gameplay routine itself is proven to require a wider arena.
- Preserve full 9-bit OAM X and validate both WRAM OAM shadow and PPU OAM after
  at least one complete VBlank.
- Use unsigned and bounds-aware logic around world X `$8000`; do not use the
  sign bit as an initializer-state test.
- Treat early object prefetch as a lead, not automatically a bug. Prove whether
  it advances motion, state, collision, allocation, or scripted behavior.
- Never claim allocator exhaustion or missing children from one actor dump.
  Correlate source records, `$192B`, allocator outcome, and lifecycle events.
- Keep the supported ROM checksum locked. Never commit ROMs, extracted assets,
  private save states, or generated game code.
- Do not edit the `snesrecomp` submodule unless the task explicitly requires it.
  Preserve unrelated dirty worktree changes.

## Debugging tool suite

Do not build a duplicate diagnostic until the existing suite has been checked.
Before debugging, read `.claude/skills/dkc1-tools/SKILL.md` and its complete
`.claude/skills/dkc1-tools/TOOLS.md` catalog. `docs/WIDESCREEN_DEBUG_TOOLS.md`
records implementation status, evidence contracts, schemas, commands, and known
limitations. If a tool is added or its contract changes, update those catalogs
in the same change.

### Runtime hosts and evidence taps

- `build/dkc1_desktop_tools.exe` is the visible interactive debugger. Its panel,
  pixel click report, F1 provenance overlay, F2-F6 plane isolation, F7 pause,
  F8 exact step, F9 flight export, and F11/F12 quick save/load should be the
  first choice when a user needs to see the repro.
- `build/dkc1_headless_tools.exe` is the deterministic batch runner for A/B,
  minimization, regression, and sweeps.
- `build/dkc1_layer_capture.exe` reloads one exact state per mask and exports
  backdrop, composite, BG1/BG2/BG3/OBJ, and occupancy masks without silently
  substituting surfaces.
- `DKC1_WS_TRACE` records per-frame presentation decisions and region/input
  hashes; analyze it with `tools/analyze_ws_trace.py`.
- `DKC1_WRAM_DUMP`, `DKC1_WRAM_HASH_LOG`, final WRAM/VRAM outputs, and
  `tools/verify_wram_dump.py` provide atomic byte evidence.
- `DKC1_OAM_LOG` plus `tools/oam_inspect.py` compares WRAM shadow and PPU OAM,
  respects the normal VBlank delay, and detects lost 9-bit X.
- `DKC1_LIFECYCLE_TRACE` plus `tools/lifecycle_by_source.py` follows actors by
  authored source record rather than mutable pool slot.
- `DKC1_BLANK_SCAN`, invariant monitoring, cache logs, retrodiction logs,
  write attribution, provenance, margin-proxy logs, prefetch guards, stream/PC
  traces, and forced cold/fallback controls are integrity diagnostics. They are
  default-off and must remain emulation-inert while disabled.
- `DKC1_FLIGHT_RECORDER` keeps rolling inputs and snapshot anchors; F9 or
  `DKC1_AUTO_EXPORT` produces a hashed repro bundle with raw memories and layer
  captures. Validate it with `tools/verify_flight_bundle.py` before analysis.

### Reproduction, routing, regression, and sweeps

| Tool | Required use |
| --- | --- |
| `run_route_recipe.py` | Validate/compile/run predicate-driven JSON routes and named atomic checkpoints. |
| `run_regression.py` | Enforce closure contracts, integrity budgets, and three byte-identical repeats. |
| `macro_minimize.py` | Delta-debug resolved input while preserving deterministic predicates and input transitions. |
| `minimize_bundle.py` | Turn an F9 flight bundle into the shortest stable repro from its anchor. |
| `world_map_fresh_entry_sweep.py` | Discover authentic controller-only entrances and archive clean pre-entry anchors. |
| `fresh_entry_stress_sweep.py` | Stress scrolling, private object windows, OAM margins, and later streaming from each clean entry. |
| `grade_fresh_entry_sweep.py` | Apply terrain, fallback, blank, and continuation release rules to fresh-entry evidence. |
| `check_widescreen_capability_floor.py` | Enforce the committed all-entrance widescreen floor so a shared calibration/presentation change cannot silently return gameplay scenes to 4:3. |
| `snapshot_widescreen_stress.py` | Stress an arbitrary supplied snapshot without touching the visible process. |
| `level_sweep.py` | Sweep the route library and grade calibration, fallback, pillarbox, margin, cache, and OAM health. |
| `run_imported_state_suite.py` | Replay imported SuperZSNES states through native and wide hosts with deterministic evidence. |
| `SuperZSNESStateExporter/` | Safely convert allowlisted v0.230 `.szst` data into a hashed portable bundle. |
| `triage_stress_lifecycle.py` | Rank lifecycle differences from stress runs by authored source and episode. |

### First-difference, transition, and visual localization

| Tool | Required use |
| --- | --- |
| `first_divergence.py` | Locate the first ordered stock-vs-wide WRAM difference, including transient divergence that later reconverges. |
| `compare_widescreen_regions.py` | Hash and compare left margin, native center, and right margin independently. |
| `detect_legacy_width_cull.py` | Detect old-256 clipping, edge repetition, and seam-shaped plane failures. |
| `bisect_transition_contamination.py` | Bisect retained-history versus cold-render contamination while holding machine state exact. |
| `transition_contamination_sentinel.py` | Run the retained/cold comparison across every hard route transition. |
| `capture_process_window.ps1` | Capture the actual application window rather than an incomplete internal surface. |
| `launch_visible_snapshot.ps1` | Open a named immutable snapshot in the visible debugger with optional trace. |
| `capture_visible_snapshot_library.ps1` | Produce the maintained visible snapshot library. |
| `validate_visible_snapshot_library.ps1` | Replay and verify that visible library against current builds. |
| `export_timeline.py` | Correlate camera, lifecycle, scanner, and widescreen events in one HTML timeline. |

### Object lifecycle, prefetch, and margin analysis

| Tool | Required use |
| --- | --- |
| `analyze_persists.py` | Classify actors that wide mode retains after stock culls from raw WRAM episodes. |
| `audit_prefetch_phases.py` | Compare stock/wide allocation episodes at the exact stock allocation frame. |
| `audit_prefetch_wram.py` | Provide the raw-byte companion to the semantic prefetch audit. |
| `audit_prefetch_transaction.py` | Prove that a simulation-neutral prefetch transaction does not leak writes. |
| `analyze_prefetch_write_sets.py` | Determine whether observed early updates are confined enough for a presentation proxy. |
| `build_margin_proxy_manifest.py` | Compile only byte-grounded, fail-closed proxy candidates into runtime data. |
| `verify_margin_proxy_ab.py` | Separate allowed presentation changes from forbidden gameplay-owned WRAM changes. |
| `verify_prefetch_soft_fallback.py` | Prove held prefetched actors release correctly across centered fallback. |
| `analyze_retrodiction.py` | Cluster margin pixels later contradicted by the cartridge stream. |
| `verify_shadow_localization.py` | Validate high-world shadow localization and world-key behavior. |
| `verify_vertical_rope_margins.py` | Validate rope culling and 9-bit OAM placement at both margins. |
| `verify_widescreen_savestate.py` | Compare split save/load execution with uninterrupted execution, including host margin history. |
| `verify_blank_scan_detector.bat` | Build and execute the blank-margin detector model oracle. |

### Source, dispatch, and reporting tools

| Tool | Required use |
| --- | --- |
| `atlas.py` | First stop for code, WRAM, symbol, caller, and evidence lookup. |
| `audit_animation_dispatch.py` | Prove animation callback coverage from byte-exact source. |
| `audit_indirect_tables.py` | Compare cfg dispatch allowlists with source-backed pointer tables. |
| `audit_dispatch_contracts.ps1` | Run the complete dispatch-contract audit against the disassembly. |
| `resolve_dispatch.py` | Convert observed RAM-pointer targets into explicit cfg contracts. |
| `export_ida_dispatch.py` | Add proven runtime dispatch xrefs/comments to the seeded IDA database. |
| `ingest_dkc1_disasm.py` | Regenerate per-bank cfg inputs from the authoritative disassembly pipeline. |
| `make_dashboard.py` | Regenerate the Markdown/HTML regression dashboard from results and known issues. |

The inventory above is a routing summary; `.claude/skills/dkc1-tools/TOOLS.md`
is the canonical command and environment-variable reference. Keep every tool
default-off unless actively requested, preserve raw evidence, and stop at the
narrowest tool that can answer the question.

## Required promotion gates

A widescreen change may become the default only after all applicable gates pass:

1. **Source/build gate**
   - Exact supported ROM checksum remains enforced.
   - Host and tool builds complete without errors.
   - Unit/model tests pass.
   - `git diff --check` passes.
2. **Determinism gate**
   - Run at least three independent replays from the same immutable root.
   - Inputs, relevant WRAM/VRAM/OAM hashes, and final frame results are
     byte-identical where deterministic equality is expected.
3. **Fresh-entry gate**
   - Test the affected scene from a clean pre-entry anchor, not only a state
     captured after initialization.
4. **Native-center gate**
   - Compare left margin, native center, and right margin independently.
   - The native center must match its accepted oracle pixel-exactly.
5. **Cross-layout gate**
   - Test more than the development room. Include representative scrolling,
     fixed-layout/bonus, vertical, underwater, boss, title/menu, and transition
     scenes when the changed code can reach them.
   - Any shared calibration, cartridge decoder, shadow, or presentation-policy
     change must run the complete 40-entrance fresh-entry matrix and pass
     `tools/check_widescreen_capability_floor.py`. A targeted-room pass cannot
     replace this floor.
6. **Transition gate**
   - Run the transition-contamination sentinel and inspect the first failure
     bundle if it does not pass.
7. **Behavioral-closure gate**
   - For objects or gameplay, prove the complete outcome: pickup, exit,
     spawn/use, boss progression, or level transition—not merely appearance.
8. **Visible QA gate**
   - Inspect the real non-headless application window in addition to raw planes.
   - Visual QA is authoritative even when generated code or IL looks valid.

No change is "fixed everywhere" merely because its original reproduction is
clean. If the full matrix has not run, state the exact validated scope.

## Standard validation commands

From the repository root:

```powershell
.\build_host.bat
.\build_host_tools.bat
$env:PYTHONDONTWRITEBYTECODE = '1'
python -m unittest discover -s tests -v
git diff --check
```

Use `tools/transition_contamination_sentinel.py` for retained-versus-cold scene
boundaries. Use `tools/level_sweep.py` and the fresh-entry tools for broader
coverage. Prefer exact input recipes and immutable snapshot anchors over manual
timing.

## Regression corpus expectations

Maintain clean pre-entry anchors and deterministic recipes for at least:

- ordinary horizontally scrolling terrain;
- Ropey Rampage or another rain/rope layout;
- Jungle Hijinxs and Jungle Hijinxs Bonus 1;
- cave foreground/BG3 cases;
- underwater object-window cases;
- moving ropes and barrel-cannon child objects;
- a boss arena using camera bounds for logic;
- title, Nintendo splash, file select, world map, bonus exit, death, and level
  transitions;
- 342x224 and optional 16:9 (398x224) presentation modes. (358x224 was
  the LEGACY emulator hack's geometry and does not exist in this host.)

When a new bug class is found, add its smallest clean fresh-entry route and its
behavioral closure to this corpus before considering the fix complete.

## Documentation and commit requirements

For every accepted fix, update the durable project record with:

- symptom and first bad frame;
- root cause and affected domain;
- exact change and why narrower/rejected alternatives were not used;
- ROM, state, executable, frame, WRAM/VRAM/OAM, and image hashes as relevant;
- commands and evidence paths;
- tested scenes/aspect ratios and untested residual risks;
- whether old save states remain historically corrupted.

Update `docs/KNOWN_ISSUES.json` and the generated dashboard when issue status
changes. Commit only intended files. Keep diagnostics default-off and normal
release behavior fail-closed.

## Live-run cleanup

Unless the user explicitly wants to keep playing, finish automated live runs by
pausing, clearing controller schedules, restoring the documented immutable root,
and recording the final process/build/state identity. Never force-kill a healthy
visible process when a graceful close is available.
