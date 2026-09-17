# DKC2 graphics and pause menu port — September 6, 2026

DKC1's Mac app now exposes DKC2's Reconstruct, CRT television, and phosphor-color options, with a native Escape menu and saved preferences. This is a host presentation change: it does not run the cartridge again, alter tile streaming, or postprocess the raw framebuffer used by state/plane diagnostics. Flat + Raw + Nearest remains the default.

## Available controls

- **Upscaler:** Nearest, Bilinear, Reconstruct, and the existing Sharp Bilinear option. Reconstruct includes all five donor modes (sharp pixels, dither decoding, diagonal edges, level-2 slopes, level-3 slopes), edge strength, softness, and smooth shading.
- **CRT television:** Living room, Studio monitor, Soft, and Custom; scanlines, sharpness, no/fine/coarse/slot phosphor mask, mask strength, glow, halation, and curvature. CRT uses its own beam pipeline and disables the flat upscaler controls. The donor's small-window mask/beam fade and brightness compensation are retained.
- **Phosphor colors:** Raw, CRT, Composite, and Trinitron, independently selectable with either display. The shared engine's existing color LUT runs on a separate presentation copy; raw pixels and machine memories remain unchanged.
- **Escape menu:** Game, Graphics, Settings, Controls, Assist, Mods, and Credits tabs. It offers immediate paused previews, window scale/fullscreen, aspect and level-edge policies, audio mute/volume, replacement music, existing two-player bindings, rewind/fast-forward, five save slots, and the Dixie runtime switch. Slot 1 retains `quicksave.state`; slots 2–5 use `slot2.state` through `slot5.state`.
- **Native menus:** View → Graphics Settings, Upscaler, Display, and Phosphor Colors; Game → Pause Menu. Escape exits fullscreen first, then opens the panel when windowed. Guide or Start + Back opens the panel. Escape/B closes it and preserves an existing debugger pause; Resume Game explicitly runs the game.

The panel uses logical AppKit window coordinates, is draggable/resizable and scrollable, and is clamped to the current screen. Simulation and audio pause at a completed frame during menu interaction. Closing it clears menu input, reanchors pacing, and resets the audio timeline. Graphics selections persist in the app's `NSUserDefaults` dictionary `GraphicsV1`; controls retain their existing preferences. The normal app needs no diagnostic environment variables.

DKC2's Windows GDI/OpenGL backend selector does not apply to this native Metal app. Its launcher bypass and alternate output sample-rate selector are not part of this graphics port; DKC1 retains the existing canonical audio/device conversion path. DKC2-specific character mods are not imported.

## Implementation and source identity

`desktop_crt.[ch]` and `desktop_filter.[ch]` adapt the current local donor's pure-C models. `macos_graphics.metal` translates the Reconstruct and CRT GLSL from DKC2's `desktop_present_sdl.c`. `macos_graphics.m` manages Metal passes, while `macos_pause_menu.m` supplies the AppKit UI and persistence. The existing immutable frame queue and fixed 60 Hz cartridge producer are retained.

The CRT pipeline performs horizontal reconstruction/linear-light decode, beam synthesis, downsampling, glow/halation blur, and final mask/curvature/composition. Reconstruct and CRT cache their final fitted image for repeated display callbacks. Pixel bytes, dimensions, settings, viewport size, and viewport origin must match before reuse. Three completion-tracked input textures prevent an upload from overwriting pixels still being read by the GPU. Intermediate/cache textures use tracked dependencies on one command queue. Strict shader math avoids threshold changes in the donor's slope reconstruction.

Donor checkout: `DKC2Recomp` HEAD `24425bf3089d853e85834fd21fc96a914726b532`, **including its current uncommitted graphics work**. It was read without modification. Private `build/repros/graphics-port-20260906/donor-manifest.json` records the exact six donor file hashes. DKC1 base HEAD is `ee6d662b75021acfdd0592324aab0acf01344f57`; final source, shader, executable, and input hashes are recorded in that evidence directory's final manifest. No ROM, extracted assets, private snapshots, or generated game code are added to source control.

## Validation and limits

Evidence directory: `build/repros/graphics-port-20260906/` (local, ignored).

- The Mac app, headless host, and standalone Metal test build. The Python suite runs 240 tests with no failures and one skip. Compiled tests cover CRT reference math, settings/clamping, all four LUT profiles, and immutable raw input.
- `test_macos_graphics` renders a synthetic edge/dither pattern and Croctopus water pixels through native transfer, all four upscalers, five Reconstruct modes, and three CRT presets. Native 1:1 RGB transfer is exact. Repeated output and cache invalidation after pixel, setting, and viewport changes match a fresh renderer exactly. Both normal-scale and 3990×2240 GPU runs pass with zero mismatches.
- A separate harness compiled the actual donor OpenGL presenter and rendered the same 342×224 water image at 1596×896. Nearest and Bilinear match exactly. Each Reconstruct mode and CRT preset differs by at most **one 8-bit channel value**. See `donor-comparison-final.json`. This measured comparison uses one water image and output size, not every scene or GPU driver.
- At 3990×2240, cached repeats took 0.11–0.25 ms in this run, versus roughly 2.7–10.1 ms to compute the tested effects. These are individual GPU timings, not a physical scanout or sustained performance guarantee.
- Actual-window QA checked Reconstruct, CRT, the graphics/settings scroll areas, fullscreen switching while the menu stayed accessible, draggable positioning, returning from the nested Controls dialog, and slot 2 save → one debugger step → load. The trace returns to guest frame 314202 with the root's raw center and WRAM/VRAM/CGRAM/OAM hashes. Screenshots are in `menu/`. The test used a separate bundle identifier and private save directory.

Nine independent 300-frame Croctopus runs (three each with Raw/Nearest, Reconstruct, and CRT) produced byte-identical full traces and final snapshots. Every frame’s raw center/margin and WRAM/VRAM/CGRAM/OAM hashes matched across all modes. Neutral input was explicitly scheduled, from guest frame 314202 through 314502. Graphics-only raw/candidate replays and final packaging results are recorded in `runtime-comparison.json` and `final-manifest.json`. Raw RGB remains the native-center oracle; selected color/shader effects intentionally change the visible presentation. This port does not broaden the aquatic fixes' validated scope or promote their source defaults. See [the water flashing investigation](WIDESCREEN_WATER_FLASH.md) for exact/fresh-entry coverage and remaining whole-game gates. Real-controller navigation and other Mac/GPU models still need user playtesting.

## Reproduce the GPU check

```sh
cmake --build build/macos --target test_macos_graphics
build/macos/test_macos_graphics runner/macos_graphics.metal
# Optional: existing output directory and a binary P6 input image.
DKC1_TEST_PIXEL_ASPECT=1 DKC1_TEST_SCALE=4 \
  build/macos/test_macos_graphics runner/macos_graphics.metal \
  /absolute/output-directory /absolute/frame.ppm
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
git diff --check
```

The normal `build/macos/DKC1Recomp.app` was rebuilt, bundled with SDL, ad-hoc signed, verified, and launched. Its per-bundle environment retains the four earlier aquatic opt-ins and the private verified ROM path, with no forced snapshot, pause, input schedule, or graphics override. The final normal app was left running for the user; graphics begin with the normal app’s saved/default choices. Source remains uncommitted.
