# 65816 SSA IR — design and implementation status

Goal: a correct intermediate representation of the byte-exact program
that structured pseudocode, backward slicing, constant propagation, and
the function differential oracle can all be built on. Until the relevant IR
stage and consumer are validated, `tools/structure.py` deliberately stays a
flat, display-only symbolizer. Its
local labels are cross-references rather than reconstructed block indentation,
and numeric define annotations are emitted only when nearby operand context
selects one ID namespace.  Transforming semantics without this layer produces
attractive lies.

Stages 1–3 now have an initial implementation under `tools/ir/`, gated by
`tools/ir_validate.py`; stages 4–5 remain design work. `tools/irview.py` is
the first read-only consumer. Its input `function` column contains mechanical
seed groups, which are not necessarily closed routines: a group may tail-fall
through into the next seed. The viewer therefore does not silently concatenate
groups. It reports the exact external continuation as a `TAIL-FALLTHROUGH`,
labels the view partial, and also surfaces unresolved indirect successors,
CFG/SSA problems, width conflicts, and blocks unreachable from the selected
entry.

## Requirements (the model must capture)

- **M/X width state** as first-class: every A/X/Y def/use annotated with
  its width; width changes (`REP`/`SEP`/`XCE`, interrupt entry) are IR
  operations. Seed from the decoder's proven static M/X facts
  (`reference/disassembly/DKC1/Pseudocode/config/`), verified at runtime
  by the existing `_M{m}X{x}` claim check in trace builds.
- **Flags** (C/Z/N/V) as SSA values with def-use chains, so branch
  conditions can be rewritten as readable comparisons ONLY when the
  defining op is proven (CMP/SBC/etc. with known operands).
- **Partial register writes**: 8-bit ops on 16-bit registers define only
  the low byte; model as sub-register defs (A.l/A.h) merged explicitly.
- **DP / DB / PB** as tracked machine state; direct-page and absolute
  operands resolve to concrete WRAM offsets only when DP/DB are proven
  constants along all paths (they usually are — assert, don't assume).
- **Bank mirroring**: canonicalize addresses through the same
  mirror-fold model the recomp/IDA pipeline uses (pc24 convention).
- **Stack discipline**: model PHA/PLA pairs, PEA-pushed return targets
  (`PEA $xxxx / JMP` idioms; the $BE8179 anim-callback contract's
  `return:810D frame:3` shape), RTS/RTL asymmetry, and stack-relative
  addressing.
- **Structure-of-arrays access idiom**: `LDA base,x` over the actor
  arrays should lift to `ActorArray[name][slot]` using the RAM map —
  typed memory, not flat bytes.
- **Indirect dispatch**: `JMP (table,x)` sites carry their runtime-proven
  contract target sets from `recomp/*.cfg` as explicit successors, with
  the contract's provenance recorded.
- **Memory side effects**: every load/store is an explicit IR op with a
  resolved (or interval) address; MMIO ($21xx/$42xx/$43xx) distinguished
  from WRAM; DMA modeled as bulk effects.

## Consumers, in build order

1. **Structured pseudocode** (replaces the symbolizer's display layer;
   keeps the 1:1 asm cross-link as ground truth).
2. **Backward data slices** ("what feeds this store to $1595?").
3. **Constant propagation + dead-code display** (fold proven DP/DB,
   define constants; never delete — annotate).
4. **Function summaries** (reads/writes/clobbers sets) — feeds the
   change-impact analyzer with data-level reachability.
5. **Function differential oracle**: capture entry state at a traced
   entry (registers, flags, DP/DB/PB, read-set snapshot), execute the
   interpreter and the native function separately, compare exit state +
   ordered write sets. The IR's read/write summaries define WHAT to
   capture and compare.

## Implementation plan (staged, verifiable)

- Stage 1: instruction decoder over `instruction_index.csv` rows into IR
  ops with width facts attached (no optimization). Validate: re-emit asm
  from IR and diff against the lossless listing — must be identical.
- Stage 2: intra-function CFG + SSA construction; branch-condition
  recovery for CMP/branch pairs only. Validate: for a corpus of curated
  functions, hand-check against the disassembly.
- Stage 3: memory typing via the RAM map (actor SoA, scanner, camera).
- Stage 4: summaries + slicing; wire into atlas.
- Stage 5: oracle harness on top of summaries + trace-entry capture.

Non-goals: whole-program optimization, decompiled-source authorship,
anything that severs the 1:1 link back to exact assembly and ROM bytes.

## Status (implemented in tools/ir/, gates in tools/ir_validate.py)

- **Stage 1 PASS** — 53,539/53,539 rows decode structurally; computed
  length == listing size column on every row; computed opcode byte ==
  actual ROM byte through the HiROM mirror fold on every row. (The
  ROM-byte gate is strictly stronger than text round-trip: it proves the
  addressing-mode classifier against silicon truth.)
- **Stage 2 PASS** — 2,557 functions, 11,401 blocks, 10,778 phis, zero
  CFG/SSA invariant failures (including the per-op use/def structural
  invariant). Labeled blocks with no intra-function path are rescued as
  externally-entered secondary roots (multi-root SSA via a virtual
  root); only 13 width-sensitive ops remain in truly dead unreferenced
  islands. M/X widths per op: 29,013 proven (REP/SEP flow +
  immediate-suffix anchors + variant-relative callee exit facts), 3,127
  call-assumed, 556 unknown (1.7% — rescued regions without anchors,
  honestly unknown-entry). Zero conflicts outside
  `tools/ir/known_discrepancies.json` — which documents ONE real find:
  the listing decodes B8:BAB9 in its M=0 view, while the recomp's
  proven M1X1 variant executes those bytes as different instructions.
- **Stage 3 PASS** — 100% of 19,949 memory operands resolved and
  classified (17,378 wram / 1,498 mmio / 738 rom / 332 indirect);
  defines resolved to fixpoint across DKC1/ + Global/ asm sources;
  NorSpr SoA accesses typed as `NorSpr[slot].Field` with curated-name
  overlay.
- **Stage 4 DONE** — `build/ir/summaries.json` (10,361 reads / 9,186
  writes, honest indirect counts); `tools/slice.py --store 1595`
  reproduces the entire damage-event chain statically (76 write sites:
  CODE_BFC745 `#$0001`, SteelKeg BFD005 `#$0040`, the `$20` raisers,
  42 clears) — cross-validated against the runtime reverse_watch
  result. Wired into `tools/atlas.py` (IR-proven writers on WRAM view)
  and `tools/impact.py` (write set + data-coupled readers).
- **Stage 5 host side DONE** — `tools/oracle_spec.py` emits per-function
  capture/compare manifests from control-flow-closed effects. Calls,
  dispatch contracts, external seed fallthroughs, and direct tail jumps
  are followed; unresolved continuations fail closed (1,066 oracle-ready
  by state diff; 1,491 need an LLE shadow due to indirect writes, MMIO
  order, deep calls, or unresolved control flow). Engine-side remainder:
  entry-state snapshot in the SNESRECOMP_FUNC_ENTRY_HOOK path,
  interpreter re-execution, exit diff.

Consumer #1 is `tools/irview.py`: structured pseudocode with proven
widths, typed operands, and recovered branch conditions, 1:1 asm links
kept per line.
