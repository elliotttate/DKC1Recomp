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
| Gameplay/presentation split | `first_divergence.py` + byte-identical WRAM A/B + `contracts/wide-intended-differences.json` | live discipline; `mod_conflicts.py` enforces it at declaration level |

Regeneration order after source changes:
`tools/ir/summarize.py` → `tools/oracle_spec.py --emit-all` →
`tools/gen_symbols.py` → `tools/gen_wram_header.py`.

## Planned: DKC1_REPLACE (routine replacement) — engine seam

Deferred until the in-flight engine work in `snesrecomp/runner/src`
(common_cpu_infra.c, cpu_trace.c) lands; the seam belongs in the ENGINE
so DKC2Recomp inherits it.

Contract for a replacement, all checks fail-closed at registration:

1. **ROM identity**: the replaced region's bytes must hash to the
   supported ROM's (stage-1 decode machinery already proves listing ↔
   ROM byte equality; reuse it).
2. **Entry mode**: assert the symbol DB's proven `entry_mode` at entry
   (the `_M{m}X{x}` claim check in trace builds already does this for
   generated code).
3. **ABI wrapper**: generated from the symbol record — registers/flags
   read and written, RTS vs RTL return, exit M/X.
4. **Validation**: a replacement of an `oracle-ready` function must pass
   the differential oracle (same harness as recomp validation: capture
   per `build/ir/oracle_specs.json`, run original and replacement, diff
   exit state + write sets) plus `impact.py`'s required regression
   contracts. `needs-lle-shadow` functions require the write-log
   compare; until that exists they are not replaceable.
5. **Fallback**: every replacement is toggleable at runtime; disabled →
   original generated routine runs. Two mods claiming one routine is a
   registration error (`mod_conflicts.py` semantics, enforced live).

Dispatch mechanics: a `{pc24 → C function}` override table consulted
before the generated entry in the dispatch path
(`cpu_dispatch_has_entry` / `dispatch_v2.c` layer).

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
- **Asset round-trip** — separate track; the disassembly repo's
  `AssetPointersAndFiles.asm` / `ExtractAssets` tooling is the start.
- **Graphical editors** — after asset round-trip.

## Non-negotiables

- `rename_map.json` is the only hand-edited name source; everything
  else is generated (drift prevention by construction, not policy).
- Accessors are views over authentic WRAM — never duplicated state.
- Presentation mods do not write gameplay WRAM, ever; margin rendering
  is host-side. Enforced by `mod_conflicts.py` and provable per-build
  with the byte-identical A/B machinery.
- Supported ROM only (headerless USA v1.0, SHA-256 fa8cacf5…f74d15).
