# Jungle night palette, signs, and level-load HD

September 16, 2026. Private HD presentation only. No cartridge, collision,
activation, or default-release changes.

Nighttime in Jungle Hijinxs is an authored CGRAM darkening, not a host bug.
The native 256x224 picture is supposed to go dark. What looked wrong was HD
falling off: signs, Rambi, and other objects snapped back to nearest-neighbor
originals, and the first frames of a level load were original pixels until
the scene guard suddenly allowed HD.

## Causes

Object identity is the SHA-256 of live CGRAM BGRA. Night (and other palette
animation) changes that hash, so resident art keyed from the ROM-extract
daytime colors misses. Backgrounds already survive this: connected world art
is looked up from canonical tile identity, then shifted by the live palette.
Objects did not.

A Sign-only silhouette table fixed the EXIT sign on the 1990-frame native
route (zero visible OBJ misses on that 32x38 sprite). It did not cover Rambi
or the Kongs, and a silhouette hit did not keep the live night colors.

Level load used two host rejects that the fade-in regularly hits:

1. `INIDISP` brightness not equal to 15. The PPU is still showing the
   picture; HD waited until the last fade step and then popped in.
2. `CGWSEL=$30` prevent-math with `CGADSUB=0`. No color math is active, but
   both `Dkc1HdScenePrepare` and `Dkc1HdSceneCaptureLine` treated prevent-math
   as a hard veto. The resident pack also waited for the first supported
   frame, so the first legal frame paid the preload cost.

## Changes

- `object-silhouettes.txt` now indexes registered sprite families (and kept
  the five harvested Sign live masks). First registered mask wins; later
  duplicate masks are skipped. Observed live duplicates are not allowed to
  evict Kong/Rambi identities.
- `object-bases.bin` (`DKHDb001`) stores the authored native colors for those
  keys. A silhouette hit borrows HD and copies that base into `art_original`
  so night CGRAM still tints the art (same delta already used for palette
  object aliases).
- Scene prepare loads the resident pack on the first prepare, including fade
  and ineligible rooms.
- Brightness 1-14 keeps HD. CPU and Metal apply `channel * brightness / 15`,
  matching the PPU. Forced blank and brightness 0 still fail closed.
- Prevent-math rejects only when `CGADSUB` is nonzero, except the existing
  cave uniform-window exception.

## Evidence

Private root: `build/hd-slice/nano-level-20260916/polish/signs-20260916/`.

| Check | Result |
| --- | --- |
| Native 1990-frame Jungle route | EXIT sign HD, dark scene, 150 silhouette hits (was 25), 0 visible OBJ misses on the 641-pixel EXIT |
| Frame 1810 late arrow | HD wooden arrow, 0 visible OBJ misses |
| Exit walk / fade-in | Forced blank still rejected; brightness 1-14 now produces supported HD frames |
| Preload tests | 9/9 including silhouette bases and malformed `object-bases.bin` |

The 1990-frame route still reports other missing OBJ samples (HUD overlaps,
unregistered composites). Those are not the 32x38 signs. DK-on-Rambi
composites remain a separate assembly problem.

World-map and other unsupported rooms still use original pixels. A fade that
lands on a room without connected art will look original until that room is
an eligible Jungle/cave scene with matching tiles.

## Identities

```text
ROM SHA-256:          fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15
entry.state SHA-256:  7aeaa4e44e714db99af7cbeb8e79e68ee0c072bf9b4e84ba35f5c927e3cc05da
Headless SHA-256:     8f2eccbbb21573833a123eb6b4a8e6da4f3733af0ac34cafba8ce3c9bb0c270e
Signed app exe:       a17b2c900f8956a37a56b10508914bc9531029d306ca7c934eb62e03a2ac5485
object-silhouettes:   6e04e1a8fc52bef0798cc4f5cf42b85bb4b79c813d18da0a97122170c8222781
object-bases.bin:     411f1443b0a1c155e0534e58d8f9e72a4de8999d7b6ad70f11bb5728eaf94287
Silhouette entries:   4924
Bases:                3673
```

Replay from repository root:

```sh
P=build/hd-slice/nano-level-20260916/polish/signs-20260916
PACK=build/hd-slice/nano-level-20260916/polish/bonus-cave-20260916/materials
DKC1_SAVESTATE_INPUT=build/hd-slice/entry.state \
SNESRECOMP_INPUT_PLAY=build/hd-slice/nano-level-20260916/polish/connected/full-route.inputs \
DKC1_HD_SCENE=1 DKC1_HD_SPRITES=1 DKC1_HD_SCENE_PACK="$PACK" \
DKC1_HD_SCENE_PRELOAD=1 DKC1_HD_EXACT_CENTERS=1 DKC1_HD_CONNECTED_WORLD=1 \
build/macos/dkc1_snesrecomp_headless "$ROM" 1990
```

The delivery app is `build/macos/DKC1 Jungle and Bonus HD.app`. Its
`LSEnvironment` still opens the cave tester state; do not point that at the
jungle EXIT evidence. The cave `user` directory was not used. No commit.
