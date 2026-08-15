#ifndef DKC1_SCRIPT_H
#define DKC1_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Deterministic route scripts with WRAM wait-predicates.
 *
 * Line format (comments with '#' or ';'):
 *   MASK [* COUNT]                  hold joypad mask for COUNT frames
 *   wait ADDR OP VALUE [mask M] [timeout N]
 *                                   neutral input until WRAM16[ADDR] OP VALUE
 *                                   (OP: == != >= <= & !&; default timeout 3600)
 *   hold MASK ADDR OP VALUE [mask M] [timeout N]
 *                                   hold MASK until the predicate passes
 *   checkpoint NAME                 record a named evidence checkpoint
 *   state_save PATH                 save a runtime snapshot after this frame
 *   state_load PATH                 load a runtime snapshot before next frame
 *
 * Waits replace fixed frame counts so route conclusions do not inherit
 * timing fragility (the emulator effort's map-entry roulette). A timed-out
 * wait fails the script: the host must exit nonzero rather than continue
 * into an undefined route.
 */

typedef struct Dkc1ScriptOps {
  const char *checkpoint;   /* non-NULL: record at the current boundary */
  const char *state_save;   /* non-NULL: save at the current boundary */
  const char *state_load;   /* non-NULL: load at the current boundary */
  bool run_frame;           /* true when the returned input must be run */
} Dkc1ScriptOps;

bool Dkc1ScriptLoad(const char *path, char *error, size_t error_size);
bool Dkc1ScriptActive(void);
/* Advance one frame: returns the joypad mask and fills ops for this frame.
 * Sets *failed when a wait timed out (message via Dkc1ScriptError). */
uint32_t Dkc1ScriptNextInput(const uint8_t *wram, Dkc1ScriptOps *ops,
                             bool *failed);
bool Dkc1ScriptFinished(void);
const char *Dkc1ScriptError(void);
void Dkc1ScriptFree(void);

#endif
