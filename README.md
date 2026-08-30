# DKC1Recomp

Static recompilation of *Donkey Kong Country* (SNES, USA v1.0) into a native
desktop application, using the project's pinned
[`snesrecomp` fork](https://github.com/elliotttate/snesrecomp)
framework — following the working [`DKC2Recomp`](https://github.com/Nicktendonick/DKC2Recomp)
project (same Rare engine family) as the template.

**Status: playable bring-up.** The project has 100% statically generated game
code, a headless validation host, playable Windows and native macOS hosts,
continuous audio, and an opt-in 342x224 widescreen presentation path. See `docs/BRINGUP.md` for the
chronological bring-up record and `docs/WIDESCREEN.md` for the widescreen
architecture, ported SuperZSNES findings, validation, and limitations.

## Why this game is a strong recomp candidate

Unlike most bring-ups, DKC1 does not need blind code discovery: the
[Yoshifanatic1 DKC1 disassembly](https://github.com/Yoshifanatic1/Donkey-Kong-Country-1-Disassembly)
(GPL-3) rebuilds the ROM byte-identically, and an IDA-based pipeline over it
(in the disassembly repo under `Tools/IDA/`) provides:

- the exact start and size of **every instruction** (53,230 instructions /
  126,790 bytes) derived from the asar symbol map — no heuristics;
- 1,276 function entries with proven entry M/X flag states;
- **complete target lists for the indirect-dispatch sites**, extracted from
  the disassembly's own `dw` jump tables (25 sites, 293 targets — including
  the 460-entry game-mode dispatcher and the animation-command table);
- ~90 semantically identified engine routines and RAM addresses.

`tools/ingest_dkc1_disasm.py` distills that into the per-bank CFG files in
`recomp/`. DKC1 executes all code from banks **80-BF** (low-RAM-mirror halves
of HiROM); the cfg banks follow that layout.

## Supported ROM

Headerless *Donkey Kong Country* USA v1.0:

| Property | Expected value |
| --- | --- |
| Size | 4,194,304 bytes |
| SHA-256 | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |

The ROM must remain outside Git. No ROM bytes, extracted assets, or generated
game code are committed.

## Debugging and validation

The widescreen port is developed from byte-exact, deterministic evidence—not
screenshots alone. [`docs/WIDESCREEN_DEBUG_TOOLS.md`](docs/WIDESCREEN_DEBUG_TOOLS.md)
defines the native debug-tool roadmap, ordered from per-frame decision traces
and replayable snapshots through object-lifecycle analysis and whole-game
level sweeps. [`docs/WIDESCREEN_HANDOFF.md`](docs/WIDESCREEN_HANDOFF.md)
records the current evidence, hashes, open issues, and release gates.

## Licensing and third-party code

Project-authored host and tooling code is MIT licensed. The pinned
`snesrecomp` framework has its own PolyForm Noncommercial license and
third-party notices. DKC structural metadata retains its documented GPL-3
disassembly provenance. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Generate the recompiled sources

```powershell
git submodule update --init --recursive
python scripts\generate_snesrecomp.py --rom "C:\private\dkc1.sfc"
```

Output lands in ignored `generated/snesrecomp/`. Regenerate `recomp/*.cfg`
from the disassembly pipeline with:

```powershell
python tools\ingest_dkc1_disasm.py --work "<disassembly>\Tools\IDA\work" --out recomp
```

Generation automatically runs the fail-closed widescreen override pass. It
adapts only source-visible culling, object activation, and direct-OAM packing;
logical camera, collision, exits, boss arenas, and tile streaming remain the
cartridge program.

## Build and run

With a Visual Studio developer environment available:

```powershell
.\build_host.bat
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
```

The desktop host enables widescreen by default. Set `DKC1_WIDESCREEN=0` for
the exact 256x224 presentation path. The headless validator accepts a frame
count and supports deterministic input playback and private frame/state
captures; its environment variables are documented in `docs/BRINGUP.md`.

### macOS

Install CMake, Ninja, and SDL2, then provide the same verified ROM the first
time private generated sources are needed:

```sh
brew install cmake ninja sdl2
./build_macos.sh "/path/to/Donkey Kong Country (USA).sfc"
open build/macos/DKC1Recomp.app
```

The arm64 app targets macOS 26 or newer, includes its SDL2 runtime, and opens a
native ROM picker when launched without arguments; the ROM is never copied into
the app. Keyboard
controls match Windows (arrows, Z/X/S/A, Q/W, Return, Right Shift), and SDL
game controllers are supported. F7 pauses, F8 steps, F9 exports an armed
flight-recorder bundle, F11/F12 quick-save/load, and Option-Return toggles
fullscreen. Native Game and View menus expose those commands plus checked
4:3, 16:10 (308x224), and 16:9 (342x224) aspect-ratio options, layer
isolation, and provenance controls. `DKC1_ASPECT=16:10` selects the
Mac-oriented mode at startup. The Mac host presents SNES pixels at their 7:6
pixel aspect; fullscreen uses a fractional nearest-neighbor fit so 16:10 fills
the display width instead of retaining an integer-scale border. Saves and
diagnostics live under macOS Application Support. For controlled launch or QA,
`DKC1_START_FULLSCREEN=1` enters the same fullscreen path after the renderer is
created. On macOS 14 and newer, the host uses a window-bound `CADisplayLink`
requested at the native 60.0988 Hz SNES cadence, prepares one cartridge frame
for its reported target presentation timestamp, and discards stale callbacks
instead of issuing catch-up bursts. Audio production follows the display
cadence macOS actually grants (60 Hz on the tested ProMotion panel). An
absolute Mach-clock scheduler remains the compatibility fallback. Set
`DKC1_FPS_STATS=1` to print renderer, display-callback, workload-phase,
submission, and present-wait telemetry when the app exits.
`DKC1_DISABLE_DISPLAY_LINK=1` and `DKC1_DISABLE_VSYNC=1` are default-off A/B
switches. `SNESRECOMP_INPUT_PLAY=path` supplies the same deterministic
per-frame input playback supported by the Windows debugger for visible Mac QA.

For visible widescreen debugging, `F7` pauses or resumes, `F8` advances one
frame while paused, and `F9` exports the rolling repro history when the flight
recorder is armed:

```powershell
$env:DKC1_FLIGHT_RECORDER = '1'
$env:DKC1_FLIGHT_RECORDER_DIR = "$env:TEMP\dkc1-repros"
.\build\dkc1_desktop.exe "C:\private\dkc1.sfc"
```

Set `DKC1_START_PAUSED=1` when opening a diagnostic snapshot to render that
exact state immediately without advancing a frame; F7 then resumes play.

The recorder keeps roughly one minute of input and periodic native snapshot
anchors in memory; it writes nothing until F9. Validate and deterministically
replay an exported bundle with:

```powershell
python tools\verify_flight_bundle.py "<bundle>" `
  --runner build\dkc1_snesrecomp_headless.exe --rom "C:\private\dkc1.sfc"
```

F11 save states written by current builds use native format v8 and include a
sparse snapshot of DKC1's host-only widescreen margin and placed-object phase
history. Older v4-v7 states remain loadable. Verify exact split-run continuity
with `tools\verify_widescreen_savestate.py`.

For intermittent culling, also set `DKC1_AUTO_EXPORT=1`. With the flight
recorder armed, a partial blank margin or a fully culled pair of margins in a
proven extended-gameplay frame automatically exports the same causal bundle
as F9. Fully black margins remain ignored on centered menus, logos, and fades.
Partial-height detection is boundary-connected: a flat band must start at the
centered 256-pixel seam and continue into the added margin. Authored holes that
begin later inside an otherwise rendered margin are not classified as culls.

For side art that appears only after a title/map/bonus/level transition, use
the clean-history oracle to distinguish retained host state from cartridge
VRAM/OAM data:

```powershell
python tools\bisect_transition_contamination.py `
  --runner build\dkc1_snesrecomp_headless.exe `
  --rom "C:\private\dkc1.sfc" --snapshot "<route-root.state>" `
  --input-play "<route-inputs.txt>" --good-frame 120 --bad-frame 180 `
  --output build\transition-bisect
```

The headless host accepts a frame count of `0` only for this diagnostic case:
it renders the exact loaded state without advancing emulation, rebuilding the
host-owned widescreen shadow from a clean history.

For an intermittent report anchored by an arbitrary native quicksave, fan the
same immutable state through deterministic movement branches without touching
the visible window:

```powershell
python tools\snapshot_widescreen_stress.py `
  --runner build\dkc1_headless_tools.exe `
  --layer-capture build\dkc1_layer_capture.exe `
  --rom "C:\private\dkc1.sfc" --snapshot quicksave.state `
  --output build\snapshot-stress --frames 420 --repeats 2
```

The runner applies fixed, diagonal, horizontal/vertical sweep, and box input
patterns. It requires exact repeat determinism and the strict margin grade. A
deterministic failure is rerun automatically with the exact trigger snapshot,
five surrounding frames, and BG1/BG2/BG3/OBJ/composite captures. Layer-capture
schema v2 also writes a backdrop-only frame and backdrop-subtracted occupancy
masks, allowing partial-height legacy-boundary culls to be distinguished from
transparent OBJ space or a shared fixed-color backdrop.

## Tool suite documentation

The complete debugging/verification tool suite (hosts and debug keys, all
env-gated evidence taps and integrity detectors, the route DSL, regression
contracts, and every script under `tools/`) is documented in one place:
`.claude/skills/dkc1-tools/` (`SKILL.md` for workflows, `TOOLS.md` for the
full reference). That guide also spells out the boundary between this
recomp's host-side widescreen and the retired asar/emulator ROM-hack
effort, which is kept only as reference material.

## Content boundary

- Never commit ROMs, saves, extracted assets, or generated game binaries.
- `recomp/*.cfg` contain only names, PCs, and structural metadata derived
  from the GPL-3 disassembly; provenance is recorded in each file header.
