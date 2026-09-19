# Dixie Kong Country in the HD macOS build

The former Baby/Kiddy Kong host overlay has been removed. The Mods menu now
contains one character option: **Dixie Kong Country**.

This is the same pinned full-cartridge variant used by the Windows build. The
stock app verifies the user's clean DKC1 USA v1.0 ROM, and the Dixie helper
synthesizes the 6 MiB mod image in memory from that ROM plus the embedded IPS
patch. The synthesized image must match the pinned SHA-256 identity before it
runs. ROMs and generated recompilation sources remain private and ignored.

The app bundle contains two native executables so their CPU, RAM, cartridge,
audio, and generated-code globals cannot overlap. Selecting Dixie relaunches
the bundled helper; selecting the menu item again returns to stock. The choice
persists, and both runtimes reuse the resolved clean-ROM path.

Dixie uses a `Dixie` subdirectory under the HD experiment's Application
Support directory. It never loads the stock HD room snapshot or stock quick
state. This keeps cartridge SRAM and state files separate.

The variant enables two compatibility rules already validated by the Windows
port: linear expanded data banks `$40-$7D`, and the mod-specific zero-size
sprite-DMA no-op. Both remain compile-time variant-only; stock cartridge
mapping and DMA behavior are unchanged.

Current validated scope is ROM synthesis/identity, fresh boot, deterministic
Jungle input, and stock/Dixie process switching. Since v0.0.17 the variant is
generated with the stock widescreen override pass and follows the saved
aspect (see `docs/DIXIE_MOD.md`, validated on Windows through the Jungle
contract; the Mac bundle has not been rebuilt with it). Dixie-specific HD
character materials are not promoted; unsupported sprite materials fall back
to authentic native rendering.
