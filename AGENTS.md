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
- 358x224 and optional 16:9 presentation modes.

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
