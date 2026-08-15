# DKC1 bring-up log

## 2026-08-15 — repo created, first generation

- Scaffolded from the DKC2Recomp layout; `snesrecomp` pinned as a submodule at
  `cd941fc` (the same revision DKC2Recomp validates against).
- `tools/ingest_dkc1_disasm.py` generated `recomp/*.cfg` from the disassembly
  pipeline (`Tools/IDA/work` in the Yoshifanatic1 disassembly repo):
  13 banks, 1,276 function entries with proven `entry_mx`, 350 interior
  data regions, 25 indirect-dispatch contracts (293 targets) resolved from the
  disassembly's own `dw` tables.
- `v2_sync_funcs_h.py` accepted all cfgs (1,276 declarations).

### Known open items

- **6 unresolved dispatch sites** read pointer tables in WRAM
  (`$0508`, `$0002`, `$0000` via B5/B6 script engines) — marked as
  `# TODO(dispatch)` comments in the cfgs. Deriving their target sets needs
  the writers of those tables enumerated (the sprite spawn-script system).
- **9 dynamic RAM-pointer jumps** (e.g. the animation per-frame callback
  `JML [$7A]` at `BE:813B`) listed in the disassembly pipeline's
  `recomp_seed/dynamic_jumps.txt`; each needs a curated contract
  (candidate values are whatever the game stores to `$1341/$130D` etc.).
- No host application yet: next milestones are (1) clean generation,
  (2) build the generated static library, (3) port DKC2Recomp's runner
  integration (`runner/`, `src/`, `app/`, CMake) and boot-probe workflow,
  (4) LLE-vs-AOT differential validation on the attract loop.

### Provenance

Structural metadata derives from the GPL-3 Yoshifanatic1 DKC1 disassembly.
Semantic names come from the 2026-08-15 IDA analysis pass documented in the
disassembly repo (`Docs/RE_Findings_DKC1.md`); several were corroborated by
the disassembly's own `RAM_Map_DKC1.asm`.
