# DKC1Recomp v0.0.17 - Windows player host with frame generation

v0.0.16 shipped the native Win32 diagnostics host as the Windows download. It
needed the ROM on the command line or beside the executable, opened with the
debug panel, closed on Escape, and had no settings, controller, save-slot,
Dixie or music controls. This release returns to the SDL player host and
carries every presentation change the native host introduced into it.

## What changed for players

- **Launch and ROM.** Double-click `DKC1Recomp.exe`. The first launch asks for
  the clean DKC1 USA v1.0 ROM (verified by SHA-256) and remembers it; later
  launches open the game directly. **Game > Change ROM...** picks another
  file and restarts the matching runtime after saves are flushed.
- **Escape opens the settings panel** (Graphics, CRT, Settings, Controls,
  Assist, Mods / Music, Credits) and never closes the game. In fullscreen the
  first Escape returns to windowed mode. Quit is **Game > Quit**, the window
  close button, or the panel's Quit button.
- **Direct3D 11 presentation** through a flip-model swap chain paced by its
  frame-latency waitable object (maximum latency one). The compositor-locked
  pacer, DwmFlush and timer modes, integer display divisor, DXGI scanout
  statistics, MMCSS thread priority and the `dkc1.pacing.v5` log are the
  native host's, now driving the full SDL feature set. The Mac graphics passes
  (nearest / bilinear / sharp bilinear / Reconstruct, CRT television with all
  presets and masks, screen color models) run as HLSL translated from the
  same Metal source. OpenGL 3.3 paced by DwmFlush is the automatic fallback.
- **Smooth animation / frame generation** (View menu or **F10**, persisted):
  held-pose interpolation at 60 Hz with about 67 ms of extra display latency,
  plus generated in-between images on 120/240 Hz displays. The half-frame
  worker, the v0.0.16 cadence rule (a real frame splits its refreshes only
  when its own midpoint follows) and the invalidation on state load, rewind,
  fast-forward, aspect change and layer isolation are all carried over.
- **View > Pixel aspect**: SNES 7:6 (authentic CRT) or square pixels.
- **No diagnostics by default.** The window title is `DKC1Recomp` (plus the
  Dixie variant name, paused state, or an isolated layer). Tier-2 coverage
  journals no longer land in the user directory; `SNESRECOMP_TIER2_CAPTURE=1`
  re-enables them. The provenance overlay, layer isolation and pacing log
  remain available from the View menu and environment for testers.
- Everything from v0.0.13 stays: dark native menus, 4:3 / 16:10 / 16:9 with
  level-edge policies, both players' keyboard/controller remapping, analog
  deadzones, Assist rewind / fast-forward / five state slots, MSU-1 music
  packs, controller stomp rumble with a test pulse, Candy saves persisting in
  `%APPDATA%/Flat2VR/DKC1Recomp/saves/save.srm`, the experimental aquatic
  widescreen opt-in, and **Mods > Dixie Kong Country** as the bundled
  `dkc1_dixie_desktop.exe` synthesized from the same clean ROM.
- **Dixie in widescreen.** The variant's generated code now receives the
  same fail-closed widescreen adaptations as stock, and the host no longer
  pins it to 4:3. Switching mods still restarts the game: Dixie is a separate
  recompiled program and cannot be swapped inside a running process.

## Packages

- `DKC1Recomp-v0.0.17-Windows-x64.zip`: the Windows SDL player host
  (`DKC1Recomp.exe`, `dkc1_dixie_desktop.exe`, `SDL2.dll`, licenses, docs,
  `BUILDINFO.json`). Windows 10/11 x64 with Direct3D 11 feature level 10.0
  or OpenGL 3.3.
- `DKC1Recomp-v0.0.15-macOS-arm64-HD-Preview.zip`: the latest macOS build,
  carried forward **unchanged** from v0.0.15 and v0.0.16 with its original
  filename and SHA-256
  `01c00aa8b410bacce08c08574ed6a41f649bb9e121dcef5e0447171a8953be0f`. It
  includes that release's first-level HD preview and Dixie support and
  requires macOS 26 on Apple Silicon. It has not been rebuilt with this
  release's Windows presenter changes or the Dixie widescreen change.

SHA-256 sidecars accompany both archives. No ROM or save state is included;
supply the verified clean DKC1 USA v1.0 ROM described in each package.

## Source changes

- `runner/windows_present.c` ports the Direct3D presenter, pacer, midpoint
  worker and frame-generation glue from `runner/win32_host.c` behind a small
  API the SDL host calls; `scripts/generate_windows_hlsl.py` translates
  `runner/macos_graphics.metal` mechanically (single-argument constructors
  become casts, `mix`/`fract` become `lerp`/`frac`, samplers stay explicit).
  The GLSL translator skips the Mac-only HD finish pass so the OpenGL fallback
  still builds.
- The v0.0.16 branch is merged back onto the v0.0.14 line with the Windows,
  iOS, save-persistence, Dixie and haptics files restored (the v0.0.15
  snapshot had dropped them). The Win32 diagnostics host keeps its frame
  generation, cadence fix and D3D presenter; it is built by `build_host.bat`
  and is not packaged.
- `scripts/generate_dixie_sources.py` applies the embedded IPS in Python, so
  the Windows build no longer needs a C compiler step or the Mac script to
  produce the Dixie sibling; `build_windows.ps1` builds both executables.
- The pinned engine advanced to `75223f3` (HD presentation and Dixie data
  banks, per-bank interpreter counters). The pinned miniz snapshot configures
  under CMake 4 through `CMAKE_POLICY_VERSION_MINIMUM`.

## Validation

- Dixie 16:9: three 16,000-frame Jungle entry repeats on the new
  `dkc1_dixie_headless.exe` are byte-identical (framebuffer, WRAM, VRAM,
  CGRAM, OAM, audio); the 4:3 `dixie-jungle` contract still passes its entry
  and quickload legs. The new `dixie-jungle-widescreen` contract passes its
  checkpoints with zero cache-bound events but fails the zero retrodiction
  budget exactly as stock's `jungle-entry` does: that stock gate has been
  failing since at least v0.0.14 (1,765 identical events on v0.0.14, v0.0.16
  and this build, unaffected by the level-edge policy) and is now recorded as
  an open known issue rather than a Dixie or merge regression. The merged
  stock build's 16:9 Jungle framebuffer and audio hashes equal v0.0.14's.
  The visible Dixie window was inspected in 16:9 at Jungle Hijinxs.
- Windows CTest: `windows_graphics` (OpenGL and Direct3D synthetic tests: 12
  modes at native, 4x and fractional sizes, exact native pixels, stable
  repeats, immutable input, source invalidation), `windows_platform`
  (23 graphics fields at min/mid/max, controls roundtrip, menu checkmarks
  including frame generation and pixel aspect, host preference roundtrips,
  fullscreen menu-bar detach/restore), `windows_music`, and both variants'
  `--haptics-test`. All pass; `runner/windows_present.c` compiles under
  `/W4 /WX`.
- Public suite: 303 tests; the five HD-preload/material-cache and
  widescreen-backport-model checks fail only on this machine's missing POSIX
  `cc` and `libm` link, as recorded for v0.0.16. New tests cover the HLSL
  translation contracts and the IPS application in the Dixie generator.
- Live 600-frame runs on a 120 Hz display with the pacing log
  (`tools/analyze_pacing.py`): waitable pacing on `d3d11`, 570 steady frames,
  0 overruns, 0 wait timeouts, submit interval p50 16.665 ms; DXGI statistics
  show every present on exactly 2 refreshes. With frame generation, 1138
  presents each on exactly 1 refresh, 540/540 midpoints presented, real to
  midpoint 8.34 ms, 0 skips. The OpenGL fallback ran on DwmFlush pacing with
  0 overruns. No tier-2 files were written to the isolated user directory.
- The built window was inspected: dark menus, the widescreen intro, Escape
  opening the settings panel with the game still running, F10 toggling
  smoothing with the View checkmark, and a clean exit through the close
  button.

## Known limits

- This is host validation, not a full-game playthrough; widescreen promotion
  gates, physical controller rumble and other GPU vendors are unchanged.
- The Mac HD preview (`v0.0.15` archive) is not rebuilt here. Its menu does
  not expose the aquatic widescreen opt-in that Windows keeps.
- The OpenGL fallback keeps 60 Hz pose smoothing but not the 120 Hz midpoint
  pairs, which need the Direct3D worker.
