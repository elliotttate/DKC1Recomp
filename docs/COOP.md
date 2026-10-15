# Simultaneous co-op

Enable **Mods > Simultaneous co-op** in the desktop host. The upper view
belongs to Donkey and the lower view to Diddy. Both use the native game
controllers, with per-player camera, object and graphics ownership. Menus
and some scripted scenes share the native picture. The setting is remembered;
loading a save state does not enable the mode automatically.

Configure both players in Controls. With no gamepad, both use their keyboard
maps. With one gamepad, Player 1 uses the keyboard and Player 2 uses the pad
and their keyboard. With two gamepads, they are assigned in connection order.
Player 2 defaults to I/J/K/L for directions, N for B, M for A and G for Y;
the other bindings are configurable. Existing nonempty custom maps are kept.

The two 224-line views form a 448-line image. Custom source widths above
342 pixels use the primary image, preserving the existing mobile width API
and the co-op save format. Windowed and fullscreen
presentation preserve its aspect ratio and fit it to the available desktop.
The mode includes independent pickups, death/barrel rescue, ropes, mounted
animals, shared minecart behavior, HUDs and serialized actor/camera state.

## Building

Initialize recursive submodules, generate the private sources using the
supported headerless USA v1.0 ROM, then use the normal release build.
`build_windows.ps1 -Rom <path>` passes that same ROM to the co-op contract
generator. For direct CMake builds, set `-DDKC1_COOP_ROM=<absolute ROM path>`;
the default is `dkc1.sfc` in the repository root. Both the ROM and generated
code stay ignored. The engine submodule remains pinned and unmodified.

The release contains no co-op debug window, per-frame log, decision log or
diagnostic actor/scheduler counters. No separate debug launcher is required.

## Scope

This port preserves the source implementation and its existing co-op save
formats. The desktop menu is the supported activation path. The existing
iOS host and its custom render-width API are retained, but no mobile co-op
controls or co-op menu are added. macOS/iOS builds require validation on Apple
hardware. Complete-game playthrough and exhaustive scene coverage remain
unverified; the local port checks are recorded below after validation.

## Port validation

- Windows Release desktop and headless hosts built successfully with MSVC.
- Public tests: 344 run, no failures, 12 skipped for unavailable optional inputs
  or platform coverage. Windows CTest: 4/4 passed.
- Source-versus-port parity: 180 frames from each of cave, underwater,
  minecart, boss and rope states, in native and widescreen modes. Final frame,
  WRAM, VRAM, CGRAM and OAM hashes match the source build.
- Three controller-only cold-boot Jungle Hijinxs runs produced identical final
  guest-memory and presentation hashes.
- Native co-op save/load: cave, underwater and minecart runs of 40 uninterrupted
  frames match 20 frames, save/load, then 20 frames, including both-view pixels.
- Custom 448-pixel width returns the primary 448x224 image. Its regression test
  failed before the width guard and passed after it was added.
- Raw two-view captures inspected. Interactive full-game play, Apple builds,
  physical controller hotplug and worst-case frame-time benchmarking are not
  covered by these checks. Private states and captures are not committed.
