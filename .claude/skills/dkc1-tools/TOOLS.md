# DKC1Recomp — complete tool reference

Companion to `SKILL.md`. Everything documented here lives in the repo;
paths are repo-relative. `<rom>` = headerless DKC1 USA v1.0.

## Build scripts (repo root)

| script | purpose |
|---|---|
| `build_host_tools.bat` | Isolated tool-session build (own obj dir/exe names, never contends with the primary session): `build/dkc1_headless_tools.exe`, `dkc1_desktop_tools.exe`, `dkc1_layer_capture.exe` |
| `build_host.bat` | Primary build: `dkc1_snesrecomp_headless.exe`, `dkc1_desktop.exe` |
| `build_host_noadapt.bat` | Builds from a generated tree WITHOUT the widescreen adapters (`build/gen_noadapt`) — the no-adapter oracle used to prove adapter inertness |
| `build_phaseguard_headless.bat` | Prefetch-phase-guard instrumented headless |
| `rebuild_widescreen_runtime.bat` | Regenerate + rebuild after recomp/cfg changes |
| `link_desktop_candidate.bat` / `rebuild_diagnostics_candidate.bat` | Link/rebuild under candidate exe names while a running visible exe holds the standard name |
| `build/link_desktop_retry.bat` | Link tool-session desktop to `_new` name, then retry the standard name |

All inject build identity (`git commit(+dirty) / config / timestamp`) shown
in the window title, debug panel, and written to `<state>.buildinfo.json`
sidecars; loading a state from a different build warns.

## Hosts

- **`dkc1_desktop_tools.exe <rom>`** — visible debugger. Dark-themed,
  menu bar (File: quick/save/load state via dialogs, export bundle;
  Emulation: pause/step; View: fullscreen, panel, provenance, FPS,
  layers). Keys: arrows+Z/X/S/A/Q/W game input, Enter=Start,
  F1 provenance overlay, F2–F6 layer isolation, F7 pause, F8 step,
  F9 export repro bundle, F11/F12 quick save/load, Alt+Enter fullscreen
  (Esc returns), Esc quit. **Click any pixel** → provenance report
  (world/tile/entry/writer/OAM/actor) in the panel.
- **`dkc1_headless_tools.exe <rom> [frames]`** — deterministic runner;
  prints end-of-run frame/WRAM/VRAM/CGRAM/OAM sha256 + audio fnv1a.
- **`dkc1_layer_capture.exe <rom> <state> <outdir>`** — same-frame layer
  isolation: reloads the snapshot per mask → backdrop/composite/BG1/BG2/
  BG3/OBJ PPMs + occupancy masks; aborts if frames differ.

## Environment variables (all default-off)

**Run control:** `DKC1_WIDESCREEN` (default 1; 0=native 4:3),
`DKC1_SCRIPT` (route .dks), `SNESRECOMP_INPUT_PLAY` (raw input replay),
`DKC1_SAVESTATE_INPUT` (load state at boot), `DKC1_SAVESTATE_OUTPUT` /
`DKC1_SAVESTATE_SAVE_AT` (save at frame), `DKC1_SRAM_INPUT`,
`DKC1_SUPERZSNES_STATE` (import emulator state bundle),
`DKC1_ROUTE_FRAME_LIMIT` / `DKC1_ROUTE_AUTOCLOSE_MS` /
`DKC1_ROUTE_RESULT` (visible-host automation), `SNESRECOMP_FPS`.

**Evidence taps:** `DKC1_WS_TRACE` (per-frame widescreen decision/hash
jsonl), `DKC1_WRAM_DUMP`=first-last + `DKC1_WRAM_DUMP_PATH` (+optional
`_RANGES`) raw WRAM frames, `DKC1_WRAM_HASH_LOG` (per-frame WRAM
fingerprint), `DKC1_WRAM_OUTPUT`/`DKC1_VRAM_OUTPUT` (final memory),
`DKC1_OAM_LOG` (shadow+PPU OAM per frame), `DKC1_LIFECYCLE_TRACE`
(+`DKC1_LIFECYCLE_SAMPLE_EVERY_FRAME`), `DKC1_INPUT_RECORD`,
`DKC1_SESSION_DIR` (checkpoint output), `DKC1_FRAME_PPM*` (frame images),
`DKC1_AUDIO_PCM`, `DKC1_STATE_TRACE`, `DKC1_STREAM_DEBUG`,
`DKC1_TRACE_PC` (PC probes on interpreter-tier execution).

**Detectors (integrity, all counted + logged):**
`DKC1_INVARIANT_MONITOR`=jsonl|1 — 9-verdict cross-subsystem monitor;
`DKC1_BLANK_SCAN`=jsonl — rendered-blank margin columns;
`SNESRECOMP_WS_CACHE_LOG`=jsonl — scene-local cache out-of-range events;
`SNESRECOMP_WS_RETRODICT`=jsonl — served-margin vs stream-truth
mismatches; `SNESRECOMP_WS_WRITE_TRACE`=1 — per-cell last-writer
attribution (powers click-to-provenance); `DKC1_WS_PROVENANCE`,
`DKC1_MARGIN_PROXIES`/`_LOG`/`_RENDER`, `DKC1_PREFETCH_PHASE_GUARD`,
`DKC1_PREFETCH_TRANSACTION_DEBUG`, `DKC1_WS_COLD_STATE_LOAD`,
`DKC1_WS_FORCE_FALLBACK_FRAME`, `SNESRECOMP_WS_YLOG`.

**Capture:** `DKC1_FLIGHT_RECORDER`=1 (+`_DIR`) — rolling ~60s inputs +
periodic state anchors; F9/auto exports a bundle (states, inputs, WRAM/
VRAM/CGRAM/dual OAM, PPU regs, manifest w/ hashes + build id, layer
captures, post-failure tail). `DKC1_AUTO_EXPORT`=1 — any detector hit
triggers the export. `DKC1_DESKTOP_DEBUG_PANEL`=0 hides the panel.

**Engine diagnostics:** `SNESRECOMP_DSPOUT`, `SNESRECOMP_*_TRACE_FILE`,
`SNESRECOMP_OFFRAILS_STDERR`, `SNESRECOMP_APU_TOUCH_CYCLES`.

## Route DSL (`recipes/*.dks`)

```
MASK [* N]                 # input mask (hex, snes9x bit order) for N frames
wait ADDR OP VALUE [width|mask|shift|signed|timeout N]
pulse MASK ON OFF [base HEX] ADDR OP VALUE [timeout N]   # press until predicate
checkpoint NAME            # WRAM/VRAM/OAM hashes + wram dump into session dir
state_save PATH / state_load PATH
```
Bits: 1=B 2=Y 4=Select 8=Start 10=Up 20=Down 40=Left 80=Right 100=A
200=X 400=L 800=R. Level-entry edge = frame counter `$0028` reset.
Routes whose first op is `state_load` are dependent legs (sweep skips
them). Contracts (`contracts/*.json`) bind a route to checkpoint
expectations, ws-trace assertions, integrity `budgets` (ratchets), and an
optional `quickload` leg seeded by a state the entry route itself saves.

## Tool catalog (`tools/`)

**Navigation / knowledge**
- `atlas.py ADDR|7EXXXX|name:TERM [--callers] [--json]` — unified query
  across IDA names+descriptions, disassembly+pseudocode, live recomp
  variant, dispatch contracts, WRAM labels, known issues.
- `export_ida_dispatch.py [--apply]` — push cfg dispatch contracts into
  the seeded IDA DB as user xrefs (idat headless).
- `ingest_dkc1_disasm.py` — disassembly ingestion used for seeding.

**Regression / sweeps**
- `run_regression.py CONTRACTS --rom R [--json-out]` — 3×-identical gate
  (checkpoints + end-of-run renderer/audio hashes + integrity budgets),
  entry + quickload legs.
- `level_sweep.py --rom R` — run every standalone recipe; grades
  calibration, raw-fallbacks, blank serves, pillarbox-in-gameplay, margin
  instability, cache OOB, OAM X-high-loss signature.
- `make_dashboard.py` — regenerate `docs/DASHBOARD.md` + `dashboard.html`
  from results + sweep + `KNOWN_ISSUES.json`.
- `fresh_entry_stress_sweep.py`, `grade_fresh_entry_sweep.py`,
  `world_map_fresh_entry_sweep.py`, `snapshot_widescreen_stress.py`,
  `run_imported_state_suite.py`, `triage_stress_lifecycle.py` —
  entry/state stress suites.

**Divergence / differential**
- `first_divergence.py --rom R --script S --frames N [--profile]` —
  stock-vs-wide first-divergence locator (resolve-then-replay, full-WRAM
  fingerprints, intended-differences profile =
  `contracts/wide-intended-differences.json`).
- `compare_widescreen_regions.py` — region differ.
- `bisect_transition_contamination.py`,
  `transition_contamination_sentinel.py`, `detect_legacy_width_cull.py` —
  transition/contamination hunters.

**Widescreen margin analysis**
- `analyze_ws_trace.py` — grade a `DKC1_WS_TRACE` (policy violations,
  fallback/blank counts, decision stats).
- `analyze_retrodiction.py LOG` — cluster served-vs-stream mismatches
  (attribute-byte vs wrong-tile, worst columns).
- `verify_shadow_localization.py`, `verify_vertical_rope_margins.py`,
  `verify_prefetch_soft_fallback.py`, `verify_margin_proxy_ab.py`,
  `build_margin_proxy_manifest.py`, `verify_widescreen_savestate.py`,
  `verify_blank_scan_detector.bat` — targeted margin verifiers.

**Object lifecycle / OAM**
- `lifecycle_by_source.py TRACE [--html-out]` — re-key lifecycle events
  by authored source record; FREED-IN-VIEW/THRASH flags; swimlane HTML.
- `oam_inspect.py` — shadow-vs-PPU OAM with 9-bit X decode, lost-X-high
  windows, DMA-lag streak logic.
- `analyze_persists.py` — grade wide-persists-stock-culls from raw WRAM.
- `audit_prefetch_phases.py`, `audit_prefetch_wram.py`,
  `audit_prefetch_transaction.py`, `analyze_prefetch_write_sets.py` —
  stock-vs-wide object phase auditors (match by source, conservative
  verdicts).
- `export_timeline.py` — event timeline export.

**Dispatch**
- `resolve_dispatch.py`, `audit_animation_dispatch.py`,
  `audit_indirect_tables.py`, `audit_dispatch_contracts.ps1` — harvest/
  verify indirect-dispatch targets feeding `recomp/*.cfg` contracts.

**Repro minimization**
- `macro_minimize.py INPUTS --predicate '{...}' [--snapshot-input]` —
  ddmin input shrink with 3×-consistency soundness.
- `minimize_bundle.py BUNDLE --rom R --predicate '{...}'` — minimize
  straight from a flight-recorder bundle (replays from its anchor).
- `verify_flight_bundle.py`, `verify_wram_dump.py` — evidence validators.
- `run_route_recipe.py` — single-recipe runner.

**Visible-host automation (PowerShell)**
- `launch_visible_snapshot.ps1`, `capture_visible_snapshot_library.ps1`,
  `validate_visible_snapshot_library.ps1`, `capture_process_window.ps1`.

## Key WRAM addresses (opcode-verified; full dictionary via atlas)

`$0028` frame ctr (resets at level entry) · `$0032` mode · `$003E`
entrance · `$0500/$0504` joypad held/pressed (P1) · `$088B/$0895` camera ·
`$1B23/$1B25` camera bounds (in-level: upper ≥ $100) · scanner `$00A0/A2/
A4` + window `$00EF/$00F1` · actor arrays indexed by even slot `$02..$32`:
id `$0D45`, source `$15FD`, x `$0B19`, y `$0BC1`, state `$1029`, anim
`$10D1`, pose `$0AE5/$0D11` · events `$1595` (**consumed same-frame**;
$40 damage, $01/$20 death → `Player_HandleHitEvents $BFA0F7`) ·
bookkeeping `$192B` len $100 · collision flags `$12A5` · invuln `$11A1`.

## Knowledge sources (read-only; addresses join them — use the atlas)

- `D:\Downloads\DKLR\DKC1_Disassembly\` — labeled disassembly + RAM map
  (semantic intent; its `Custom\Patches\*Widescreen*` and `ROM_Map_HACK_*`
  belong to the LEGACY hack — ignore for recomp work)
- `...\DKC1\Pseudocode\` — mechanical C lift + lossless listing +
  `instruction_index.csv` (a stale recomp snapshot; never diagnose recomp
  behavior from it)
- `...\Tools\IDA\DKC1_U1.i64` + `work\rename_map.json` — curated names/
  descriptions + our runtime dispatch xrefs; rebuildable headlessly
