# Baby Kong mod

Baby Kong is an optional native-host mod that presents Donkey Kong as Kiddy
Kong and gives him a heavier, Kiddy-inspired movement profile. It is off by
default and changes neither the supported DKC1 ROM nor any generated source.

## Use on macOS

1. Launch `DKC1Recomp.app` with the supported DKC1 ROM.
2. Choose **Mods > Choose DKC3 ROM...** and select the headerless North
   American (En,Fr) DKC3 ROM.
3. Use **Mods > Baby Kong** to turn the mod on or off. The choice and the ROM
   path persist between launches; the ROM itself is never copied.

For controlled runs, set both variables before launching the desktop or
headless host:

```sh
DKC1_BABY_KONG_ROM="/path/to/Donkey Kong Country 3 (USA).sfc" \
DKC1_BABY_KONG=1 \
build/macos/dkc1_snesrecomp_headless \
  "/path/to/Donkey Kong Country (USA).sfc" 600
```

`DKC1_BABY_KONG=0` forces the stock path. The DKC3 ROM must be the exact
4 MiB headerless image with SHA-256
`2277a2d8dddb01fe5cb0ae9a0fa225d42b3a11adccaeafa18e3c339b3794a32b`.
An absent or mismatched ROM leaves the mod disabled.

## What changes

- All 354 gameplay frames in the Kiddy sprite map are decoded in memory from
  the verified user-owned DKC3 ROM. No graphics or palettes are stored in the
  repository, app bundle, preferences, or save data.
- The native renderer identifies Donkey's exact contiguous OAM run, removes
  only those OBJ pixels from the composite, and aligns the Kiddy frame to the
  captured native sprite's opaque lower edge. This remains stable when DKC1's
  airborne OAM wraps through scanline 255. Diddy, enemies, particles,
  collision, and level rendering stay on the original DKC1 path.
- Walk, run, roll, jump, land, idle-look, and swim frames are selected from
  DKC1 actor state, velocity, and input. Facing direction follows the original
  actor.
- Holding run while grounded builds roll momentum up to a capped speed. In
  the air, a new jump press shortens the rise and the heavier body accelerates
  downward faster. DKC1 remains the collision and interaction oracle.

This is a Kiddy-inspired moveset fitted to DKC1's systems, not a transplant of
DKC3's entire player engine. Team-up throws, water skipping, partner-specific
level scripting, and DKC3-only collision states do not have equivalent DKC1
systems and are not claimed here.

## Validation

The stock disabled path must retain the native DKC1 framebuffer and machine
hashes. The private integration check uses an external DKC1 snapshot and both
external ROMs to prove that enabling the mod changes the framebuffer, keeps
ROM-derived data in memory, and exercises the movement path without putting
private output in Git. Source-only unit coverage is in
`tests/test_baby_kong_mod.py`.

The sprite decoder reconstructs DKC3's 16-tile-wide virtual VRAM layout,
including its optional second DMA segment. It does not assume that the four
tiles of a 16x16 piece are consecutive in the ROM payload.
