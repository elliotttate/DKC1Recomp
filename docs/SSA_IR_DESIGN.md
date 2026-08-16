# 65816 SSA IR — design spec (pre-implementation)

Goal: a correct intermediate representation of the byte-exact program
that structured pseudocode, backward slicing, constant propagation, and
the function differential oracle can all be built on. Until it exists,
`tools/structure.py` deliberately stays a display-only symbolizer —
transforming semantics without this layer produces attractive lies.

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
