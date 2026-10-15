#ifndef DKC1_COOP_FORMATIONS_H
#define DKC1_COOP_FORMATIONS_H
#include <stdint.h>

/* Shared record decoder; callers retain separate collision/visibility policy.
 * Result -1 ends the authored list, 0 rejects malformed storage, 1 is valid. */
typedef struct Dkc1CoopFormationRecord {
  uint16_t address, x, y, bytes, bitmap, flags;
} Dkc1CoopFormationRecord;
static inline uint16_t Dkc1CoopFormationWord(const uint8_t *ram,unsigned a) {
  return (uint16_t)(ram[a]|((uint16_t)ram[a+1]<<8));
}
static inline int Dkc1CoopFormationRead(const uint8_t *ram,unsigned a,
                                       Dkc1CoopFormationRecord *record) {
  if (!ram || !record || a<0xc000 || a>=0xd000 || (a&15)) return 0;
  record->address=(uint16_t)a;
  record->x=Dkc1CoopFormationWord(ram,a);
  if (record->x==0xffff) return -1;
  record->y=Dkc1CoopFormationWord(ram,a+2);
  record->bytes=Dkc1CoopFormationWord(ram,a+4);
  record->flags=Dkc1CoopFormationWord(ram,a+8);
  record->bitmap=Dkc1CoopFormationWord(ram,a+10);
  return record->bytes && !(record->bytes&1) && record->bytes<=0x1fe &&
      record->bitmap>=0xd000 && record->bytes<=0x10000u-record->bitmap;
}

/* One native large (16x16) OBJ. Preserve x bit 8 when packing private OAM.
 * The callback may flush OAM pages; it must not mutate ram or rom. */
typedef void (*Dkc1CoopFormationEmit)(void *context, int16_t x, uint8_t y,
                                     uint16_t tile_attributes);

/* Pure B8B9B5 presentation of the shared formation bitmap. The entire entrance
 * list is initialized at B8B580; $C2 is only a camera-local traversal cursor.
 * Does not call BB2E collection, allocate actors, or change cartridge state.
 * rom is the supported headerless 4MiB image, ram is the 128KiB WRAM image. */
unsigned Dkc1CoopFormationsDraw(const uint8_t *ram, const uint8_t *rom,
                              uint16_t camera_x, uint16_t camera_y,
                              Dkc1CoopFormationEmit emit, void *context);
/* Same bitmap and phase, with host-only horizontal margins (0..128 pixels).
 * The native center stays identical; wider visibility never collects pieces. */
unsigned Dkc1CoopFormationsDrawWide(const uint8_t *ram, const uint8_t *rom,
                                  uint16_t camera_x, uint16_t camera_y,
                                  unsigned extra_left, unsigned extra_right,
                                  Dkc1CoopFormationEmit emit, void *context);
#endif
