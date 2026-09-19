# Dixie Kong mod (ROM-hack port) — design + status

Status: **playable experimental Windows variant; core Jungle movement, ponytail
spin, barrel rescue, character swap, death/re-entry and deterministic save/load
tested. The reported map corruption is fixed; Jungle/Ropey map navigation,
normal map reload and Ropey entry pass. Not full-game validated.**

See [the September 14 map fix](DIXIE_MAP_FIX_2026-09-14.md) for current mapping
and save evidence, and [the September 13 QA report](DIXIE_QA_2026-09-13.md)
for the earlier launch/build fixes and gameplay coverage.

This document is the durable record for porting the "Dixie Kong Country" IPS
ROM hack (user-supplied, `C:\Users\ellio\Downloads\Dixie Kong Country\Dixie Kong Country.ips`)
into the native recomp. Stock DKC1 behavior, hashes, and the stock evidence
chain remain separate. The earlier port depends on a variant-only engine DMA
workaround described below. The September 14 map fix also changes the shared
cartridge resolver, with its expanded mapping enabled only by the Dixie variant.

## How to use (optional setting)

The mod is an opt-in **Mods → Dixie Kong Country** toggle and needs **no
extra ROM**: the variant executable synthesizes the modded 6 MiB image in
memory from your verified clean DKC1 ROM plus the embedded IPS patch, and
refuses to run unless the synthesized image matches the pinned mod identity.

1. Toggle **Mods → Dixie Kong Country** on. Because the mod is a full
   recompilation variant, enabling it relaunches the sibling
   `dkc1_dixie_desktop.exe`, which takes over the session. The choice
   persists (registry `HKCU\Software\DKC1Recomp\Mods`, value `Dixie`), so
   the app keeps starting as Dixie Kong Country until you turn it off.
2. In the variant, the same menu item switches back to stock (relaunches
   `DKC1Recomp.exe` in the SDL app or `dkc1_desktop.exe` in the debugger).
   Switching restarts the game; it does not transfer the current session state.

Baby Kong (the Kiddy Kong presentation) was removed from the Mods menu when
Dixie shipped; its persisted state is ignored and its env vars no longer
activate it. The Mods surface now offers exactly one character option.

Headless/automation contract:

```bat
:: stock headless hands the session to the variant executable; the clean-ROM
:: argument flows through and the variant synthesizes the mod from it:
set DKC1_DIXIE=1
build\dkc1_snesrecomp_headless.exe C:\path\to\clean.sfc 600

:: force stock even when the persisted setting is on:
set DKC1_DIXIE=0
```

Both exes must ship side by side. Missing sibling executables leave the current
app in charge and report the launch failure. An invalid ROM is rejected by its
loader. Menu restarts now preserve the resolved ROM path, including paths with
spaces; explicit menu choices override an inherited `DKC1_DIXIE` setting.
The regular Windows SDL variant returns to `DKC1Recomp.exe`; the debugger
variant returns to `dkc1_desktop.exe`.

## Repository note: embedded mod patch

`runner/dkc1_dixie_patch.inc` (and its generator
`scripts/generate_dixie_patch_include.py`) embeds the hack's public IPS patch
into the variant build. This is a deliberate, user-directed exception to the
project's "never commit extracted assets" rule, made so the mod needs no
external patched ROM; the embedded bytes are the same patch the hack's author
distributes and are useless without the user's verified clean ROM. Remove the
include and the `Dkc1DixieLoadRom` synthesis path to revert.

## How to build (development)

```bat
:: one-time: generate the variant's private sources from the clean ROM. The
:: script applies the embedded IPS in Python, checks the pinned identity and
:: runs the same fail-closed widescreen override pass as stock (v0.0.17;
:: pass --no-widescreen-overrides for the v0.0.13 native-only variant).
python scripts/generate_dixie_sources.py --rom "<clean 4MiB rom>"

:: player build: build_windows.ps1 -Rom <rom> builds DKC1Recomp.exe,
:: dkc1_dixie_desktop.exe and dkc1_dixie_headless.exe through CMake.
:: debugger/headless variants via the direct scripts (stock first):
build_host.bat
build_host_dixie.bat

:: run the variant directly with EITHER the clean ROM (synthesizes the mod)
:: or an already-patched ROM file:
build\dkc1_dixie_headless.exe "C:\path\to\clean.sfc" 600
build\dkc1_dixie_desktop.exe  "C:\path\to\clean.sfc"
```

`build_host_dixie_tools.bat` builds a profile/watch-enabled debug exe.
For the regular Windows SDL app, after generating the private variant sources:

```powershell
cmake -S . -B build-windows/release -DDKC1_BUILD_DIXIE_VARIANT=ON
cmake --build build-windows/release --parallel 6
ctest --test-dir build-windows/release --output-on-failure
```

This opt-in builds `DKC1Recomp.exe` and `dkc1_dixie_desktop.exe` beside each
other with the same SDL frontend. Keep `SDL2.dll` alongside them. This does
not update an existing release archive or implement the Mac variant.
The patched ROM is produced by applying the IPS to the supported clean ROM
(headerless USA v1.0, sha256 `fa8cacf5…f74d15`) and is **6 MiB**, sha256
`2769b72a8a2050000336f5dd6dea1a45385f4f35ee710dafb0c0a3592295643b`.
It is accepted only by the separate Dixie runtime loader and by
`scripts/generate_snesrecomp.py` for variant generation. The stock runtime
rejects the modded ROM rather than running it against stock generated code.
Never commit ROMs or generated code.

## What the mod IS (established facts)

- The IPS changes the original 4 MiB and expands the file to 6 MiB. The
  nonzero expansion data is in file banks `$40-$41`; it includes additional
  sprite layouts and graphics. The previous description of this as new code
  in CPU bank `$C0` was incorrect.
- DKC1's file is **HiROM-ordered** (the `0x31` map byte is a known mislabel).
  Banks `$80-$BF` mirror `$00-$3F`, and the **game itself uses banks `$C0-$FF`
  as aliases of `$00-$3F`** via `& 0x3F` (proven by unpatched code reading
  `$C9:2D95` for the boot SPC-upload table and `$E9:D6DC` for a VRAM upload).
  This fold remains correct for those aliases; an experiment mapping the
  whole `$C0-$DF` range to the extension hangs the boot at that SPC table, and
  a bank-`$C0`-only extension mapping regressed the world map. However, folding
  the mod's low data banks `$40-$41` is incorrect: pose entry `$BB:F8A8`
  points to `$41:6000`, whose layout is at file offset `0x416000`, not
  `0x016000`. The verified variant now opts into bounded linear `$40-$7D`
  data addressing; stock HiROM and all `$C0-$FF` aliases retain their mapping.
- The hack is a character/animation replacement: small in-place code edits
  (boot/init, gameplay banks `$B9/$BB/$BC/$BE`), patched animation-frame tile
  data in bank `$02` (six 896-byte frame slots, first tiles of each slot
  replaced), expanded sprite data, and repointed animation tables.
- Recompiling the patched ROM against the **stock cfg set** analyzes cleanly:
  2567 roots → 2693 AOT variants (stock: 2692), 0 LLE-only.

## Validation state

The September 14 mapping correction fixes fresh map entry and the reported
Ropey Rampage map. Three independent controller-only refreshes from the
preserved tester state produce byte-identical snapshots and preserve lives,
bananas, completion flags, selected Kong and unlocked named map nodes.
Ropey level entry, Jungle spin, stock native output and save continuity pass.
The actual rebuilt SDL window is visually clean. An old snapshot can retain
already-corrupt VRAM until a normal scene reload; no serialized-memory repair
was added. The tester's active slot was refreshed through normal inputs and
the original retained separately. Full-game coverage remains incomplete.

Entry and quickload regression legs each pass three independent byte-identical
runs. The corrected SDL startup route also matches the headless image and
WRAM/VRAM/CGRAM/PPU OAM/OAM-shadow hashes in three fresh runs. Stock rejects the
6 MiB image; the Dixie loader accepts either the verified clean ROM or the
exact pinned patched image and produces identical 600-frame results.

## Earlier level-DMA workaround (partial visual recovery)

The earlier port recovered playable Jungle Hijinxs (entrance `$0016`) through
a variant-only DMA workaround. These are the earlier investigation's findings,
not a new source audit: this checkout currently lacks the disassembly library
described by `reference/README.md`.

1. **Root cause.** The mod's sprite-DMA queue (WRAM `$170F+X`) emits
   entries whose 16-bit size field is 0. The stock consumer
   (`NorSpr_ProcessGraphicsDMAQueue`, `$8B:8CB0`) loads that field straight
   into DMA channel 0's `$4305`, and a raw size of 0 means **65536 bytes**
   - so every such entry overwrote 64 KiB of VRAM at `$2118` each frame
   (`SNESRECOMP_DMA_LOG` diff vs stock; leaf attribution via
   `SNESRECOMP_DMAQ_ZERO_WATCH` points at the queue-size stores in
   `CODE_BBA849`). Stock's only legitimate zero-size VRAM DMAs are the
   boot uploads from bank `$80`, which keep working.
2. **Fix.** `dma_set_zero_size_vram_noop(1)` (variant hosts only, default
   off): a general DMA to `$2118` with raw size 0 from a cartridge bank
   other than `$80` is treated as a no-op - reproducing the behavior of
   the emulator the hack was built against (the author's own disclaimer
   is “This does not work on console”). Implemented in
   `snes/dma.c`; the sprite queue's zero-size entries become no-ops and
   the intended 896-byte frame uploads proceed.

That workaround remains a compatibility dependency. Both QA passes preserved
its behavior; v0.0.13 pins it together with the map resolver in engine commit
`de94587464524ebac8dcd62e14618bf0d3953644`. It did not resolve
the map sprite corruption; the independently proven mapping correction above
does. No general hardware-accuracy or whole-game claim follows from these
targeted results.

The [v0.0.13 release record](RELEASE_0.0.13.md) covers the published Windows
package and retained v0.0.12 save-persistence integration. The paired Mac
archive is the unchanged v0.0.9 build and does not contain Dixie.
## Widescreen (v0.0.17)

The v0.0.13 variant was generated without the presentation-widescreen
overrides and v0.0.15 locked the host to 4:3 for it. Both were choices, not
limits: `scripts/apply_dkc1_widescreen_overrides.py` matches all 34 of its
anchors in the Dixie generated units (the mod changes graphics and sprite
data, not the culling, activation or OAM-packing code the pass adapts), so
`generate_dixie_sources.py` now applies it and the host follows the saved
aspect. `contracts/dixie-jungle-widescreen.json` is the stock `jungle-entry`
gate run on the variant (`dkc1_dixie_headless.exe`, built by
`build_windows.ps1`). On 2026-09-19 it passed its checkpoints with zero
cache-bound events, and three 16,000-frame 16:9 repeats of the route were
byte-identical in framebuffer, WRAM, VRAM, CGRAM, OAM and audio hashes. Its
retrodiction budget fails exactly as stock's `jungle-entry` does on the same
build and on v0.0.14/v0.0.16 (1,791 versus 1,765 events over the same 67
frames; see `jungle-retrodiction-ratchet` in `docs/KNOWN_ISSUES.json`), so
the Dixie 16:9 margins are as good as stock's, no better. Other scenes have
the same fail-closed behavior as stock (black margins where reconstruction
cannot be proven) and no separate Dixie promotion.

Switching between stock and Dixie restarts the game. The variant is a
separate recompiled program (its own generated translation units); running
both in one process would need every generated symbol namespaced and would
double the executable, so the sibling-process design stays.

## Non-goals (v1)

- Full-game visual and behavioral certification.
- Mac variant build and relaunch support.
