# DKC1 modding layer — architecture and status

A stable, named layer above the mechanically generated recomp.
Generated C stays disposable build output; mods target names, typed
memory, and events — never `bankXX_part*.c` internals.

```text
ROM + disassembly + IDA rename_map + recomp cfgs + IR + runtime evidence
        ↓ (generators — nothing hand-edited except rename_map)
build/ir/symbols.json          one canonical record per function
runner/dkc1_wram_gen.h         typed views over authentic WRAM
        ↓
mechanically recompiled routines  +  validated C replacements (planned)
        ↓
mod declarations + conflict checking + the existing evidence gates
```

## What exists today

| Piece | Where | Status |
|---|---|---|
| Canonical symbol DB | `tools/gen_symbols.py` → `build/ir/symbols.json` | live — names+provenance, proven entry/exit M/X, symbolic read/write sets, callers, dispatch roles, runtime-route evidence, oracle eligibility |
| Typed WRAM accessors | `tools/gen_wram_header.py` → `runner/dkc1_wram_gen.h` | live — 88 named offsets, 53 actor SoA field accessors, struct mirrors; little-endian views over live WRAM, never copies; `--check` staleness + cross-parser gate |
| Conflict checking | `tools/mod_conflicts.py` | live — routine conflicts, WRAM write-set overlap, presentation-class violations, oracle-eligibility and no-runtime-evidence warnings |
| Deterministic mod tests | recipes/contracts/`run_regression.py`/`first_divergence.py`/`promote_bundle.py` | live — this IS the mod test framework; point contracts at a modded build |
| Differential oracle | engine capture (`SNESRECOMP_ORACLE*` in the trace build) + `tools/oracle_run.py` + `tools/oracle_diff.py` | live — per-call entry/exit registers, flags, WRAM ranges, cycle delta; two deterministic legs must match byte-for-byte |
| Routine replacement | `tools/gen_replacements.py` + `runner/replacements/` + `build_host_replace.bat` | live — see DKC1_REPLACE below; demo replacement proven equivalent |
| Gameplay/presentation split | `first_divergence.py` + byte-identical WRAM A/B + `contracts/wide-intended-differences.json` | live discipline; `mod_conflicts.py` enforces it at declaration level |
| Baby Kong reference mod | `runner/dkc1_baby_kong.*` + `docs/BABY_KONG_MOD.md` | live — opt-in, hash-gated DKC3 ROM decode; Kiddy presentation plus isolated movement tuning; stock path fails closed |

Regeneration order after source changes:
`tools/ir/summarize.py` → `tools/oracle_spec.py --emit-all` →
`tools/gen_symbols.py` → `tools/gen_wram_header.py`.

## DKC1_REPLACE (routine replacement) — LIVE

Mechanism: **link-level variant takeover**. Generated call sites invoke
the M/X variant symbol (`CODE_BDF88A_M0X0`) directly, so
`tools/gen_replacements.py` stages a build override that recompiles the
defining generated TU with the variant renamed to `*_original` and
links `runner/replacements/dkc1_replacements.c` in its place
(`build_host_replace.bat` → `build/dkc1_headless_replace_trace.exe`).
Every call site — direct, dispatch, alias — reaches the replacement;
the untouched original remains the runtime fallback
(`DKC1_REPLACE_DISABLE=1`).

Contract, all checks fail-closed at staging:

1. **ROM identity**: supported-ROM sha + the replaced region's actual
   ROM bytes (entry..end from cfg facts, mirror fold) must hash to the
   manifest's blessed `region_sha256`.
2. **Entry mode**: manifest mode must equal the proven entry facts.
3. **Single definer**: exactly one generated TU may define the variant.
4. **Validation**: the differential oracle. `oracle_run.py` on the
   stock trace exe vs the replace exe must produce byte-identical
   capture logs (registers, flags byte, WRAM ranges, cycle delta at
   every outermost call), plus identical end-of-run
   frame/WRAM/VRAM hashes, plus `impact.py`'s required contracts.
5. **Fallback**: `DKC1_REPLACE_DISABLE=1` runs originals; two mods
   claiming one routine is a `mod_conflicts.py` error.

Proven on `CODE_BDF88A` (object-scanner window): readable C with the
widescreen adapters preserved; 980/980 oracle calls and all end-of-run
hashes byte-identical to stock on route_jungle, with the disabled leg
separately proving the fallback path. Replacements must replicate cycle
accounting and flag semantics exactly (the demo shows the pattern:
mechanical prologue/epilogue copied from the generated original — a
future wrapper generator's job — around a readable core).

## Planned: semantic events — EXPERIMENTAL tier only

The trace hook (`cpu_trace_func_entry`) is already a function-entry
event system. Events wrap runtime-PROVEN boundaries only:

| Event | Proven anchor |
|---|---|
| OnLevelLoaded | `$0028` frame-counter reset edge (level entry) |
| OnPlayerDamaged | `Player_HandleHitEvents` consuming `$1595` == $40 |
| OnPlayerDeath | `$1595` == $01/$20 consumption path |
| OnCameraChanged | `$088B/$0895` delta |
| BeforeActorUpdate / AfterActorUpdate | per-slot dispatch via the state-machine catalog |

Stabilization rule: an event graduates from experimental only with a
regression contract exercising it and runtime evidence in the profile
corpus. With ~20% function coverage and one degraded-proven scene,
promising API stability beyond that would be dishonest.

## Deferred (deliberately)

- **Mod manifests / launcher / ordering** — one mod exists today; the
  useful early piece (WRAM ownership conflicts) shipped in
  `mod_conflicts.py` instead.
- **General asset round-trip** — separate track. Baby Kong is a deliberately
  narrow exception that decodes verified user-owned ROM data in memory and
  never exports it or turns it into a repository asset.
- **Graphical editors** — after asset round-trip.

## Non-negotiables

- `rename_map.json` is the only hand-edited name source; everything
  else is generated (drift prevention by construction, not policy).
- Accessors are views over authentic WRAM — never duplicated state.
- Presentation mods do not write gameplay WRAM, ever; margin rendering
  is host-side. Enforced by `mod_conflicts.py` and provable per-build
  with the byte-identical A/B machinery.
- Supported ROM only (headerless USA v1.0, SHA-256 fa8cacf5…f74d15).
