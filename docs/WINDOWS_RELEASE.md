# Windows SDL release

Current Windows release: [v0.0.17, player host with frame generation](RELEASE_0.0.17.md).
The sections below retain the earlier host validation history.

## v0.0.17 Direct3D presenter, pacing and frame generation

The SDL host presents through `runner/windows_present.c`: a Direct3D 11
flip-model swap chain (three buffers, frame-latency waitable object, maximum
latency one, `DKC1_MAX_FRAME_LATENCY=2` to widen it) that runs the Mac graphics
passes as HLSL generated at build time by `scripts/generate_windows_hlsl.py`.
The viewport, pixel-aspect and pass sequence are the OpenGL presenter's, which
remains the fallback (`DKC1_PRESENTER=opengl`) and is still built and tested.

Pacing follows the native host: a display whose refresh divides to 59.5-60.5 Hz
locks the emulated frame to an integer divisor and waits on the swap chain's
waitable object (`waitable`); the OpenGL fallback waits on `DwmFlush`
(`dwmflush`); other rates or `DKC1_PRESENT_HZ` use the absolute QPC timer
(`timer`). Wait-first ordering samples the controller as late as possible in
the locked modes; timer mode and the 120 Hz pair keep work-first ordering. The
emulation thread joins the MMCSS Games class and opts out of power throttling.
`DKC1_PACING_LOG` writes the asynchronous `dkc1.pacing.v5` record with DXGI
scanout statistics; the Mac pacing log is not opened on Windows.

**View > Smooth animation / frame generation** (F10, `[Host] FrameGen`,
`DKC1_FRAMEGEN=1|force`) enables the four-frame pose interpolation and, on an
even display divisor with Direct3D, the midpoint worker that presents the
generated in-between image on the following refresh. A frame splits its
refreshes only when it produced a midpoint; stepping, rewind, fast-forward,
layer isolation, the provenance overlay, state loads and aspect changes drop
the history so no image is built across a jump. **View > Pixel aspect**
(`[Host] SquarePixels`, `DKC1_SQUARE_PIXELS=1`) presents square pixels instead
of the 7:6 CRT pixel.

**Game > Change ROM...** verifies and remembers a new ROM, exits after the
in-game save flush and restarts the matching stock or Dixie executable. The
window title is the product name plus variant, paused state and any isolated
layer. Tier-2 coverage journals go to the `NUL` device unless
`SNESRECOMP_TIER2_CAPTURE=1` or an explicit tier-2 path is set.

`DKC1Recomp.exe --graphics-test` runs the OpenGL test and then the Direct3D
test (readback of the flip-model back buffer before presentation). See the
[v0.0.17 release notes](RELEASE_0.0.17.md) for the measured pacing evidence.

## v0.0.12 in-game saves

Candy saves now persist between launches in
`%APPDATA%/Flat2VR/DKC1Recomp/saves/save.srm`. The host loads cartridge SRAM
before boot and atomically writes changed data. Read/write failures are
reported; malformed files and failed writes preserve the previous save.
Save-state formats are unchanged. See [save recovery and validation](INGAME_SAVES.md).
The v0.0.12 binary release is Windows x64 only; older Mac downloads still have
the missing SRAM persistence and are not repackaged as fixed builds.

The v0.0.10 Windows host shares `sdl_host.c` and the Mac host's graphics,
audio-rate, input, refresh, rewind, CRT and color-filter models. Windows supplies
native dark menus/settings, INI preferences, file pickers, memory mapping,
QPC/high-resolution deadline waits, and OpenGL 3.3 presentation. The older Win32
debug host remains available. No Mac Objective-C, Metal source, cartridge
adapter, generated dispatch policy or submodule was replaced.

## Build and launch

Use Windows 10/11 x64, a GPU/driver supporting OpenGL 3.3, Python 3, Git,
CMake, Ninja and Visual Studio 2022 C++ desktop tools. In an x64 Developer
PowerShell:

```powershell
.\build_windows.ps1 -Rom 'C:\private\Donkey Kong Country (USA).sfc'
& .\build-windows\release\DKC1Recomp.exe
```

The script also generates and builds the Dixie sibling (`-SkipDixie` omits
it) and runs CTest plus the public suite (`-SkipPublicTests`). Double-clicking
the release executable opens a ROM picker the first time and remembers the
verified path. Keep `SDL2.dll` and `dkc1_dixie_desktop.exe` beside the
executable; the MSVC runtime and miniz are statically linked. The
supported headerless 4 MiB ROM has SHA-256
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
No ROM, extracted assets or private state belongs in the release ZIP.

## Mac feature mapping

| Controls | Windows implementation |
| --- | --- |
| Graphics / CRT | All 23 persisted graphics fields; Nearest, Bilinear, Sharp Bilinear, Reconstruct; five reconstruction modes including dither decoding; strength/softness/shading; CRT presets, masks, scanlines, sharpness, glow, halation and curvature; v0.0.17 renders them through Direct3D 11 (OpenGL fallback) |
| Screen colors | Same Raw, CRT, Composite and Trinitron lookup tables |
| View | 4:3, 16:10 and 16:9; window scale, fullscreen, Reflect/Bars/Shift/Glide; layer isolation and provenance; v0.0.17 adds Smooth animation / frame generation and Pixel aspect |
| Controls | Both players' keyboard/gamepad bindings, source routing and analog deadzones; controller pause navigation |
| Assist / states | Opt-in rewind, 3x fast-forward, four remappable host actions, five independent state slots |
| Sound | Canonical game audio, host-only drift correction, mute/volume; MSU-1 folder or bounded `.msu1`/ZIP import |
| Mods | v0.0.13: optional Dixie Kong Country sibling, verified clean DKC1-ROM synthesis, saved enable state; replaces the previous Baby Kong menu option |
| Aquatic presentation | Same five experimental flags; saved opt-in applies next launch and remains off by default |

Escape opens the pause panel (exits fullscreen first), F7 pauses/resumes, F8
steps, F11/F12 save/load the selected slot, F9 exports a private repro, and
Alt+Enter toggles fullscreen. Game/View/Mods/Music expose the native dropdowns.
The menu bar is detached while fullscreen and reattached on return to windowed
mode, so the borderless fullscreen drawable covers the whole display.
Settings are in `%APPDATA%/Flat2VR/DKC1Recomp/windows.ini`, beside user states.
The Game menu exposes **Controller rumble (enemy stomps)** and a test pulse.
It defaults on and persists under `[Host] Haptics`; `DKC1_HAPTICS=0/1`
overrides that preference at startup. See [the Dixie haptics fix](DIXIE_HAPTICS.md).
`DKC1_USER_DIR` redirects both Windows settings and states to an existing
absolute private directory. It does not modify Mac NSUserDefaults.

The renderer translates the project's seven Metal passes at build time into
GLSL, retaining the shader arithmetic and shared parameter derivation. It reads
only completed immutable pixels. Mac CADisplayLink/Metal is retained on Mac;
Windows uses QPC deadlines and one main-thread GL submission. This is not a
claim of identical scanout behavior or Mac hardware validation on Windows.

## v0.0.11 fullscreen menu bar

A Win32 menu bar is non-client area, so it stayed drawn across the top of the
borderless `SDL_WINDOW_FULLSCREEN_DESKTOP` window and shortened the OpenGL
drawable by its height. `SetFullscreen` now calls `Dkc1WindowsShowMenuBar(0)`
before entering fullscreen and `Dkc1WindowsShowMenuBar(1)` after leaving it,
before `ApplyWindowedSize` so SDL's frame adjustment accounts for the bar. The
HMENU and its checkmarks persist across detachment; `windows_platform` asserts
detach/restore and retained menu state. Attaching the bar at startup had also
taken its height from the freshly created client area (1197x628 instead of
1197x672 at 3x/16:9 on a 200% display), so the host now reapplies the windowed
size after `Dkc1MacInstallMenu`. No Mac, cartridge or widescreen change.

Live check on the private ROM (isolated `DKC1_USER_DIR`, 3456x2170 display):
View > Fullscreen and Alt+Enter both left `GetMenu()` NULL with a `WS_POPUP`
window whose client area equalled the monitor; the menu item, Alt+Enter and
Escape returns each restored the bar and a 1197x672 client at the original
position. Screenshots showed the boot logos across the whole display with no
bar.

## Verification and limits

- Public suite: 243 tests, 10 expected skips (unavailable external references
  and GCC-only checks); MSVC now runs the portable host/graphics models.
- CTest: real GPU shader tests (12 modes at native, 4x and fractional sizes;
  exact native RGB, repeat stability, source invalidation, immutable input),
  graphics/control preference roundtrips and native menu checkmarks, and safe
  ZIP extraction plus actual memory-mapped synthetic PCM mixing/reset.
- Private ROM: nine independent 360-frame launches, three aspects times
  nearest/reconstruction/CRT. All complete snapshots had SHA-256
  `19903b8778defb18711a60588ba6114b55565589ffbd5bc2169df039f7c70dc5`.
- Fresh Jungle Hijinxs entry uses the input/wait-only prefix of
  `recipes/route_jungle.dks` (7,332 startup frames); the actual Windows game
  window and high-DPI dark settings were inspected. QA files remain ignored
  under `build-windows/` and do not touch normal user slots.
- The live panel applied CRT and switched 16:9 to 16:10 immediately, persisting
  `display=1` and `aspect=1`. Both-player bindings and the audio/five-slot
  controls were inspected. A 480-host-frame Jungle replay exercised Assist
  save/load, rewind and 3x fast-forward with a separately verified DKC3 ROM
  supplied for Baby Kong. The synthetic PCM test proves mixing, not listening
  coverage of a complete replacement soundtrack.

This is targeted host validation, not a start-to-finish playthrough or the
40-entrance widescreen promotion gate. Existing widescreen limitations remain.
Physical controller rumble, every controller model, other GPU vendors and
actual Mac hardware were not verified here. The paired Mac archive is the
unaltered v0.0.9 release, not a Mac binary rebuilt on Windows.
