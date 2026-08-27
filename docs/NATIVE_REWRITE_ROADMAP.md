# Native rewrite roadmap

## Architecture decision

Keep DKC1Recomp as the executable foundation. Use
`reference/disassembly/` as the authoritative source for routine boundaries,
tables, constants, control flow, and semantic naming. The disassembly is not a
better runtime by itself: it rebuilds a 65816 ROM, while DKC1Recomp already
provides native hosting, deterministic state, widescreen presentation, audio,
and the regression/debug infrastructure needed to prove replacements.

Native replacements should therefore be introduced incrementally behind the
existing dispatch/replacement layer. A replacement is retained only when it is
state- and frame-identical to the generated implementation on the route corpus.

## Measurements (2026-08-27)

Representative Jungle state, `/O1`, 358/16:9 renderer:

- Recompiled SNES frame execution: approximately 0.78 ms/frame.
- PPU draw/presentation: approximately 0.94 ms/frame before this pass.
- SPC/APU work inside frame execution: approximately 0.28 ms/frame.
- Audio output handoff: approximately 0.003 ms/frame.

The retained production build sustained about 809 headless frames/second on
the development PC over a 10,000-frame state run. Headless throughput is a
diagnostic ceiling, not a presentation pacing target.

### Retained changes

1. `PpuViewportAllows` is forced inline. `/O1` previously emitted a real
   function called for each nontransparent background pixel. Across repeated
   3,000-frame profiles, median main-background time fell from 888 ms to 688 ms
   (about 22.5%) with identical framebuffer/state hashes.
2. The stack-balance hash-table auditor is compiled out of normal player
   builds and retained in tool builds. It is diagnostic bookkeeping, not the
   semantic recomp stack. Median recompiled-frame time over repeated
   5,000-frame runs fell from 3,967 ms to 3,603 ms (about 9.2%). Interpreter/AOT
   return handling and call-stack tracking remain enabled.
3. Function-scoped write logging no longer performs a prefix `strncmp` on
   every generated function entry when `SNESRECOMP_WLOG` is unset. When the
   logger is armed, its historical default prefix and override still work.
4. Tool builds expose `SNESRECOMP_PPU_PROFILE=1`, reporting sprite, main/sub
   background, widescreen enhancer, and composition CPU time at process exit.
   Normal player builds compile this profiler out.

### Rejected experiments

- A decoded 4bpp tile-row cache was pixel-identical but about 4% slower. The
  existing packed-bit unrolling is already efficient; cache lookup/maintenance
  cost exceeded saved decode work.
- A converted-CGRAM palette cache was pixel-identical but about 3% slower.
  DKC's frequent mid-screen palette writes invalidate it too often.
- Splitting/inlining generic overlay and layer-policy cold paths helped the
  underwater scene slightly but made Jungle about 11% slower, most likely from
  code growth and incorrect assumptions about which policies are cold.
- Broad `/O2` compilation was hash-identical but did not reliably beat `/O1`;
  the generated program is large enough for instruction-cache pressure to
  matter.

## Next native targets

1. **Production generated-function ABI.** Keep the semantic recomp stack and
   deadline/interpreter bridge, but move optional profiling, boundary auditing,
   snapshots, and post-mortem accounting behind tool-build gates. Measure each
   gate independently; do not delete return-context machinery.
2. **Named hot game routines.** Use function-entry profiles plus the
   disassembly to replace one routine at a time with readable native C. Start
   with high-count animation/object dispatch routines, not visually sensitive
   level streaming. Every replacement must preserve registers, flags, cycles,
   stack/return context, and memory-write order until the oracle proves those
   effects irrelevant.
3. **Mode-1 DKC scanline specialization.** Implement a fail-closed fast path
   for a measured common PPU state, with the current renderer as the oracle and
   fallback. Mid-scanline palette/window changes, mosaic, overlays, and unusual
   color math must reject the fast path rather than approximate.
4. **SPC700/APU optimization.** It is roughly a third of recompiled-frame CPU
   time in the measured Jungle state. Prefer native basic-block translation or
   a proven optimized SPC core behind an audio/state oracle; do not HLE the
   music driver unless cue timing and all port traffic are reproduced.
5. **Persistent framebuffer/direct presentation.** This remains useful for
   desktop smoothness, but it is separate from game-code native conversion and
   must not weaken the raw framebuffer oracle.

## Required proof gate

For every retained optimization:

1. Benchmark an interleaved before/after sequence on at least Jungle and one
   distinct renderer class such as underwater.
2. Compare framebuffer, WRAM, VRAM, CGRAM, OAM, and audio hashes.
3. Run the full `unittest` suite.
4. Run representative native save states, including a transition/bonus state.
5. Keep diagnostic behavior available in the tool build when production gates
   it off.

Performance claims without an oracle match are exploratory, not shippable.
