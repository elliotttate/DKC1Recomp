#ifndef DKC1_DIXIE_MOD_H
#define DKC1_DIXIE_MOD_H

#include <stddef.h>
#include <stdint.h>

/* Optional "Dixie Kong Country" mod switch, in the same spirit as the other
 * Mods-menu settings: a persisted toggle that decides which executable
 * starts. The mod needs no external ROM file: the variant build synthesizes
 * the modded 6 MiB image in memory from the user's verified clean DKC1 ROM
 * plus the embedded IPS patch, and refuses to run unless the synthesized
 * image matches the pinned mod identity. See docs/DIXIE_HD_MAC.md.
 *
 * Hosts call Dkc1DixieHandoffCheck() at the top of main(); when it returns
 * nonzero the process must exit immediately (the sibling has been spawned).
 * DKC1_DIXIE_VARIANT builds are the variant executable and only ever hand off
 * back to stock, and only from an explicit menu choice.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Reads/writes the persisted setting (registry on Windows, env elsewhere).
 * Enabling also clears any legacy saved mod-ROM path: the ROM is synthesized
 * now, so there is nothing to pick. */
int Dkc1DixieSavedEnabled(void);
void Dkc1DixieSetEnabled(int enabled);

/* 1 when this build IS the variant executable (compile-time). */
int Dkc1DixieIsVariant(void);

/* Remember the resolved ROM path so a menu restart uses the same cartridge. */
void Dkc1DixieSetRomPath(const char *path);

/* VARIANT builds only: loads the mod ROM image. Accepts the user's pinned
 * clean ROM (synthesizes the modded image in memory and verifies it against
 * the pinned mod identity) or an already-patched ROM (identity-verified).
 * Returns a malloc-owned 0x600000-byte buffer, or NULL with `error` set. */
uint8_t *Dkc1DixieLoadRom(const char *path, size_t *size_out, char *error,
                          size_t error_size);

/* Early startup gate. `variant_exe` is the sibling executable name to spawn
 * into when the setting is on ("dkc1_dixie_desktop.exe" /
 * "dkc1_dixie_headless.exe" for stock builds). The current argv is passed
 * through, so the sibling receives the same clean-ROM argument; env
 * DKC1_DIXIE_ROM overrides that argument (automation aid), and DKC1_DIXIE=0
 * forces stock even when the persisted setting is on. Returns 1 after
 * spawning (the caller must exit). On any failure returns 0 with the setting
 * cleared and `note` (optional) describing why. */
int Dkc1DixieHandoffCheck(int argc, char **argv, const char *variant_exe,
                          char *note, size_t note_size);

/* Menu action: persists `enabled`, then spawns the sibling (variant when
 * enabling from stock, stock when disabling from the variant) and exits the
 * current process. Never returns on success; on spawn failure rolls the
 * setting back, fills `note`, and returns so the caller stays put. */
void Dkc1DixieSwitchAndRelaunch(int enabled, const char *variant_exe,
                                const char *stock_exe, char *note,
                                size_t note_size);

#ifdef __cplusplus
}
#endif

#endif
