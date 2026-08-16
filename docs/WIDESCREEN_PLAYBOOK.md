# Widescreen fix playbook — from symptom to verified fix

The decision guide that ties the whole tool suite together. Tool
reference: `.claude/skills/dkc1-tools/TOOLS.md`. Architecture:
`docs/WIDESCREEN.md`. History/chronicle: `docs/WIDESCREEN_DEBUG_TOOLS.md`
and `docs/BRINGUP.md`. Registry of every known class:
`docs/KNOWN_ISSUES.json` — **check it first; most new symptoms are a
known class recurring.**

## How widescreen issues surface (five entry points)

1. **Visible glitch** in `dkc1_desktop_tools.exe` — click the pixel
   (provenance report), F9 exports a flight bundle.
2. **Detector jsonl** from a headless route (invariant monitor,
   retrodiction, blank scan, cache log, ws-trace policy classes).
3. **Contract/regression failure** (`run_regression.py` budget or hash).
4. **Sweep grade** (`level_sweep.py` → cache/OAM-wrap/blank columns,
   `capability_manifest.py` downgrade).
5. **Playtester capture** — promote it first
   (`promote_bundle.py`) so the symptom is a pinned, replayable asset
   before any analysis.

## Step 0 — identity, coverage, priors

- Pin the scene identity: mode/entrance/source (panel, bundle manifest,
  or `$0032/$003E` at the bad frame). Grep `docs/KNOWN_ISSUES.json` for
  the symptom and scene; check `docs/COVERAGE.md` — if the scene is
  never-observed, record a route before debugging anything.
- Confirm build identity (window title / state sidecars). A
  post-corruption save state is **historical evidence, not fresh-entry
  proof** — reproduce from a fresh route before trusting any conclusion
  (the jungle-bonus1 investigation was misled by exactly this).

## The localization ladder

Descend only when the current rung can't name the culprit. Each rung
narrows: pixel → tile → subsystem → policy/frame → WRAM byte → game
intent → instruction.

1. **Pixel → provenance.** Click in the visible host: world coord,
   tile, cache entry provenance, last writer kind+frame
   (`SNESRECOMP_WS_WRITE_TRACE`), OAM entries, nearest actor. The click
   report ends with the exact `reverse_watch` follow-up command.
2. **One frame, isolated layers.** `dkc1_layer_capture.exe` (or F2–F6
   live) — same-frame BG1/BG2/BG3/OBJ/backdrop isolation tells you
   WHICH layer carries the artifact and whether it's margin-only.
3. **Frame history of the tile/actor.** Tile write tracer
   (`WsShadowDebugLastWriter`), `lifecycle_by_source.py` (actors by
   source record `$15FD`, never by pool slot), `export_timeline.py`.
4. **Policy stream.** `DKC1_WS_TRACE` + `analyze_ws_trace.py`: was the
   frame widened/calibrated/centered/raw-fallback, which decision
   changed at the bad frame; `analyze_retrodiction.py` for
   generated-vs-truth margin content.
5. **Is it widescreen at all?** `first_divergence.py` stock-vs-wide:
   byte-identical WRAM proves the simulation is untouched and the bug
   is pure presentation (fix in host/ws_shadow); WRAM divergence means
   an adapter or policy leaked into gameplay — treat as highest
   severity. `build_host_noadapt.bat` is the no-adapter oracle.
6. **Who wrote the bad WRAM value.**
   `reverse_watch.py --address A:len --before-frame F` (function-level
   attribution in one deterministic pass); `slice.py --store A` for the
   static writer set with SSA value slices.
7. **What the game intends.** `atlas.py <addr>` joins every knowledge
   source; `irview.py <fn>` renders validated structured pseudocode
   (proven widths, typed operands, recovered branch conditions);
   `impact.py <fn>` gives blast radius + required gates before editing
   anything the function touches. Key widescreen addresses: camera
   `$088B/$0895`, scanner window `$00EF/$00F1` (computed by
   `CODE_BDF88A`, already adapter-widened), streamer VRAM base `$1B13`,
   camera bounds `$1B23/$1B25`, actor SoA (`build/ir/symbols.json`).
8. **Instruction-exact.** `SNESRECOMP_ORACLE` capture on the suspect
   function (`oracle_run.py` / `oracle_diff.py`) or force_lle +
   `DKC1_TRACE_PC` for store-level attribution.

## Verdict / detector → action table

| Signal | Meaning | First tool | Usual owner |
|---|---|---|---|
| `actor_visible_but_no_oam` | simulated actor in view, no sprite | rung 3 (lifecycle by source), then OAM inspector | dkc1_game visibility policy |
| `oam_xhigh_lost` | 9-bit X high bit dropped, low byte continuous — art teleports left | `level_sweep.py` oam_wrap.first_suspect, then rung 6 on the OAM shadow | entry-time bias window (open class: entry-oam-xhigh-loss) |
| `ppu_oam_stalled` | shadow OAM advances, PPU copy doesn't | `DKC1_OAM_LOG` shadow-vs-PPU | host upload path |
| `simulated_outside_wide_window` | actor active beyond the widened window | rung 4 + `scanner_window_unexpected` | scanner-widening adapter sites |
| `bookmark_advanced_without_actor` | `$192B` bookkeeping moved, actor never seen | rung 3 gated by source-unseen window | object scan policy |
| `margin_tile_never_streamed` | margin cell shown but never received truth | retrodiction log + rung 1 provenance | ws_shadow serving policy |
| `stale_generated_margin` | guessed margin cell outlived its scene | write tracer (writer kind `prefill_guess`/`decode_force`) | ws_shadow invalidation |
| `cache_window_violation` | access outside the scene-local window | `SNESRECOMP_WS_CACHE_LOG` (oob accounting) | ws_shadow origin/rebase |
| `scanner_window_unexpected` | `$EF/$F1` width ≠ 0x140+2·extra | rung 7 on `CODE_BDF88A` (see `runner/replacements/`) | adapter constants |
| retrodiction mismatch | generated margin ≠ later stream truth | `analyze_retrodiction.py`, then rung 6 on the decode source | decode path (fixed class: margin-decode-attribute-mismatch) |
| blank-scan columns | rendered-blank margin during gameplay | rung 2 (which layer), rung 4 (policy) | prefill/serving |
| cache oob / rebase storm | window churn | cache log + camera trace | scene-local origin policy |

## Where the fix goes (and how to prove it)

- **`snesrecomp/runner/src/snes/ws_shadow.c`** — margin cache serving,
  provenance, scene-local window. Engine change: keep it game-agnostic.
- **`runner/dkc1_video.c` / `dkc1_game.c`** — calibration, layout
  capability policy, prefill, presentation bias. The proven safe
  default is **fail-closed stock behavior** with widening as opt-in:
  the widened cartridge initializer corrupted layouts it didn't
  understand until it was contained behind
  `DKC1_ENABLE_EXPERIMENTAL_CARTRIDGE_WIDENING=1` (see
  jungle-bonus1/ropey-rampage entries). When in doubt, pillarbox.
- **Generated visibility adapters** — via `recomp/*.cfg` + regenerate,
  never by editing `generated/`.
- **`DKC1_REPLACE`** (docs/MOD_LAYER.md) — when a routine needs
  readable surgical change: stage with `gen_replacements.py`, prove
  with the differential oracle (byte-identical capture logs + end-run
  hashes, as done for `CODE_BDF88A`).

Verification ladder, in order, all required:
1. Arming any tap is A/B hash-inert.
2. Stock-vs-wide WRAM byte-identical (presentation fixes) or the
   intended-differences contract updated deliberately.
3. `run_regression.py` on the required gates from `impact.py`, 3×
   byte-identical including end-of-run framebuffer/audio hashes;
   ratchet the detector budget for the fixed class to 0.
4. `level_sweep.py` + `capability_manifest.py` + `coverage_explorer.py`
   refresh — a fix that downgrades another scene is not done.
5. Update `docs/KNOWN_ISSUES.json` (`fixed_note` with the proof), keep
   the repro bundle path, `make_dashboard.py`.

## Worked examples (read these before your first fix)

- **jungle-bonus1-widened-initializer-corruption** — visible corruption
  → flight bundle → layer isolation → binary bisection across archived
  builds located the policy commit → fix = fail-closed stock
  initializer default → 3× fresh-entry replays + independent
  Ropey Rampage A/B. The lesson: capability must be *proven per
  layout*, never inferred from a shared routine.
- **wide-seven-tile-stream-guard-gap** — one stale 8px strip → renderer
  consumed seven tiles where init/sweep prepared six → count contract
  aligned, then superseded by the safer stock default. The lesson:
  ring-buffer coverage contracts must match the consumer exactly.
- **margin-decode-attribute-mismatch** — invisible until the
  retrodiction verifier compared generated margins to later stream
  truth (1,690 mismatches), fixed upstream, budget ratcheted to 0. The
  lesson: add the verifier for content you generate.

## Anti-patterns

- Editing `generated/` or copying legacy-hack (`358x224`) code.
- Diagnosing from a post-corruption save state as if it were fresh.
- Comparing against the legacy patched ROM as "stock".
- End-of-frame WRAM dumps for same-frame events (`$1595`): watch
  transitions and durable effects instead.
- Concluding "not reproducible" from a route that never reaches the
  scene — check `coverage_explorer.py` first, record a route, promote
  the capture.
- Fixing without a pinned repro + contract: it will regress silently.
