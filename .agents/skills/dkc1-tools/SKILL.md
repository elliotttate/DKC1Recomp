---
name: dkc1-tools
description: Working guide to the DKC1Recomp debugging/verification tool suite — builds, hosts, routes, detectors, regression gates, and the code atlas. Use when debugging widescreen/gameplay issues, running evidence captures, or navigating the disassembly knowledge sources.
---

# DKC1Recomp tool suite

This repo is a **static recompilation** of Donkey Kong Country (SNES, USA
v1.0) with **host-side widescreen presentation**. Everything here follows
one contract: game logic stays byte-for-byte stock; widescreen is
presentation only (margins, camera bias, wider object *visibility* — never
changed *simulation*). The supported ROM is headerless USA v1.0,
sha256 `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`
(never commit ROMs, save states, captures, or generated game code).

## ⚠ Do not confuse the two widescreen efforts

There were TWO widescreen projects. Only this repo is current:

| | **This repo (current)** | **Legacy emulator hack (reference only)** |
|---|---|---|
| Approach | Host-side presentation over stock logic | ROM patched with asar (game code modified) |
| Location | `C:\Users\ellio\Documents\GitHub\DKC1Recomp` | in-repo consolidated copy: `reference/legacy-widescreen/` (canonical for reading); originals at `D:\Downloads\DKLR\DKC-Widescreen-358x224\`, `D:\Downloads\DKLR\DKC_Widescreen_358x224*.sfc` (+398 variants, MSU1 builds), `reference/disassembly/DKC1/Custom\Patches\Widescreen_358x224.asm`, `RomMap\ROM_Map_HACK_*` |
| Use for | All new work | Prior-art reference: its worklog documents bug classes (OAM X-high wrap, exit-boundary coverage, section controllers) that inspired our detectors. **Never copy its code or compare its patched-ROM behavior as "stock".** |

If a file mentions `358x224`, asar patching, or lives under
`reference/legacy-widescreen/` or under `D:\Downloads\DKLR\` outside the
disassembly's clean sources, it is the legacy effort. (Careful: the
RECOMP host renders 342x224 / 398x224 — a `398x224` mention alone does
not mean legacy; `358x224` always does.)

## Repo map

- `runner/` — host + adapters (`dkc1_game.c` presentation glue,
  `win32_host.c` visible debugger, `headless_main.c`, detectors)
- `snesrecomp/` — engine submodule (**fork: elliotttate/snesrecomp**;
  `runner/src/snes/ws_shadow.c` is the widescreen margin cache)
- `recomp/*.cfg` — recompiler configs incl. **dispatch contracts**
  (runtime-proven indirect-call target sets). Fixes for
  recompilation-correctness bugs go HERE, then regenerate.
- `generated/` — machine-emitted game C. **Never hand-edit or commit.**
- `tools/` — the analysis/verification suite (see `TOOLS.md` here)
- `recipes/*.dks` — deterministic input routes; `contracts/*.json` —
  regression contracts; `docs/KNOWN_ISSUES.json` + `docs/DASHBOARD.md`
- Build: `build_host_tools.bat` (isolated tool-session build →
  `build/dkc1_headless_tools.exe`, `dkc1_desktop_tools.exe`,
  `dkc1_layer_capture.exe`), `build_host.bat` (primary),
  `build_host_noadapt.bat` (no-widescreen-adapter oracle). All embed build
  identity (git commit shown in window title / state sidecars).

## Where fixes go (never into decompiled/generated code)

1. **Presentation/widescreen bug** → `ws_shadow.c`, `dkc1_game.c`/
   `dkc1_video.c` adapters, or the hosts.
2. **Recomp-correctness bug** (wrong dispatch, miscompile) →
   `recomp/*.cfg`, regenerate.
3. To understand intent, use the knowledge sources via
   **`python tools/atlas.py <addr|7Exxxx|name:term>`** — one query joins
   IDA's curated names/descriptions, disassembly+pseudocode listing, the
   live recompiled variant, dispatch contracts, WRAM labels, and
   known-issue mentions. Debug output (verdicts, click reports, trace PCs)
   pastes straight in.

## Evidence discipline (non-negotiable)

- Detectors/taps are **default-off** (env-gated); arming one must never
  change emulation (A/B hash-identical).
- Watch **transitions, not dumps**; match actors by **source record
  ($15FD)**, never by mutable pool slot.
- Routes are **predicate-driven** (`wait`/`pulse` on WRAM), resolved to
  fixed input schedules before differential comparison.
- A fix is verified only by **3× byte-identical replays** including
  end-of-run framebuffer/audio hashes (`tools/run_regression.py` enforces
  this plus ratcheted integrity budgets).
- Event flags like `$1595` are consumed same-frame — end-of-frame WRAM
  dumps cannot see them; watch durable effects (state, timers) instead.
- WRAM semantics: see `tools/atlas.py wram:<addr>`; key addresses are in
  `TOOLS.md`.

## Typical workflows

**Private Magnific sprite experiment:** use `extract_jungle_sprite_inventory.py`
for checksum-locked Jungle family extraction and `magnific_precision_experiment.py`
for exact plate preparation/assembly. See `TOOLS.md` and
`docs/MAGNIFIC_PRECISION_V2_EXPERIMENT.md`. Provider submission is a separate MCP
step; source-family coverage does not prove all live composite materials.
`magnific_scene_materials.py` covers captured background context aliases;
`magnific_banana_hud.py` builds every normal banana count from verified primitives.
For resident level packs, `build_hd_preload_manifest.py PACK` validates/indexes
all private rasters; `DKC1_HD_SCENE_PRELOAD=1` loads them on the first prepare,
including fade and ineligible scenes, and prevents gameplay texture reads.
`build_hd_object_silhouettes.py` indexes exact object masks and authored native
colors so night/CGRAM-shifted live captures reuse registered art. See the
preload and trace contracts in `TOOLS.md`.
For the connected Nano scenery experiment, use `audit_hd_jungle_map.py`,
`build_hd_connected_pack.py`, and `build_hd_stream_boundaries.py`; the last tool
accepts only byte-identical source subregions. Use `hd_sprite_matte.py` on a new
private pack to remove generation-matte fringes without keying out gray paint.
See `docs/HD_NANO_POLISH_REVIEW.md` for bounded coverage and visual acceptance.

**Widescreen bug of ANY kind:** start at
**`docs/WIDESCREEN_PLAYBOOK.md`** — the symptom-first decision guide:
five entry points, the eight-rung localization ladder (pixel →
provenance → layers → policy → stock-vs-wide → reverse_watch →
atlas/irview/slice → oracle), the verdict/detector → owner table for
every invariant-monitor verdict, fix-ownership routing incl.
DKC1_REPLACE, the full verification ladder, and worked examples from
docs/KNOWN_ISSUES.json.

**"Something looks wrong on screen" (visible host):** click the pixel —
the panel reports world coord, tile, provenance, last writer, OAM entries,
nearest actor. F9 exports a repro bundle (rolling inputs + states + memory
+ auto layer-isolation captures). With `DKC1_AUTO_EXPORT=1` +
`DKC1_FLIGHT_RECORDER=1`, any integrity detector exports automatically and
appends a post-failure tail.

**Reproduce + diagnose headlessly:** run a recipe with taps armed, e.g.
`DKC1_WIDESCREEN=1 DKC1_SCRIPT=recipes/route_jungle.dks DKC1_WS_TRACE=t.jsonl
build\dkc1_headless_tools.exe <rom> 9000`, then the matching analyzer
(`analyze_ws_trace.py`, `lifecycle_by_source.py`, `analyze_retrodiction.py`…).

**Gate a fix:** `python tools/run_regression.py contracts/jungle-entry.json
--exe build/dkc1_headless_tools.exe --rom <rom> --json-out
build/regression/results.json`, then `python tools/make_dashboard.py`.
Sweep everything: `python tools/level_sweep.py --rom <rom>`.

**Pin a new bug:** add it to `docs/KNOWN_ISSUES.json` with a repro; ratchet
its detector count in the contract `budgets`; tighten to 0 when fixed.

Full catalog of all 47 tools, every env var, route DSL, and debug keys:
**`TOOLS.md` in this folder.**

For the default-off Mac HD GPU compositor, use `verify_hd_scene.py --preload
--metal` with the indexed private pack. This performs CPU/GPU pixel comparison
on every eligible frame; disable validation for timing. `test_hd_metal.py` covers
immutable packets and GPU pool lifetime. Metal options and trace fields are
listed in TOOLS.md under Local HD scene experiment.

For HD scenery popping during scrolling, preserve the tester state and use
`verify_hd_scene.py --cases replay --state STATE --input-play INPUTS --frames N
--coverage` plus the earlier clean-entry route. Compare source-stable scenery
in motion; zero missing-material counts alone do not prove stable art identity.
See `docs/HD_NANO_POPIN_REVIEW.md` for the connected-subtile fallback case.

For the private Jungle Bonus 1 HD cave, use the exact tester state plus the
controller-only outdoor/Rambi/bonus/exit route in `docs/HD_NANO_BONUS_REVIEW.md`.
`connected-cave.bin` is a separate source-verified world; check color-window
semantics and OBJ palette math before widening any HD scene capability.
`build_hd_object_composites.py` fills only fully source-covered OBJ combinations.
