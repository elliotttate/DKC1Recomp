#include "dkc1_coop_formations.h"

static unsigned DrawRecord(const uint8_t *ram, const uint8_t *rom,
                           const Dkc1CoopFormationRecord *record,
                           uint16_t camera_x, uint16_t camera_y,
                           unsigned extra_left, unsigned extra_right,
                           Dkc1CoopFormationEmit emit, void *context) {
  const uint8_t *bank = rom + 0x380000;
  const unsigned a=record->address, fx=record->x, fy=record->y;
  const unsigned bytes=record->bytes, bitmap=record->bitmap;
  const int dx = (int)fx - camera_x, dy = (int)fy - camera_y;
  /* B918 admits only these rows before B9B5 indexes its vertical mask tables.
   * Bitmap lengths are bytes; X/Y become 8-bit at BA52. Invalid records fail
   * closed instead of following a corrupt pointer into unrelated WRAM. */
  if (dy < -135 || dy >= 224 ||
      dx >= 256 + (int)extra_right)
    return 0;
  const uint16_t mask = dy < 0 ? Dkc1CoopFormationWord(bank, 0xbcd8 + ((dy + 143) >> 3) * 2) :
      Dkc1CoopFormationWord(bank, 0xbcfa + (dy < 96 ? 0 : ((dy - 96) >> 3) * 2));
  unsigned first = 0, length;
  const int wide = extra_left || extra_right;
  if (wide) {
    const int lower = -(int)extra_left - 15;
    if (dx < lower) first = (unsigned)((lower - dx + 7) / 8) * 2;
    unsigned end = (unsigned)((255 + (int)extra_right - dx) / 8 + 1) * 2;
    if (end > bytes) end = bytes;
    if (first >= end) return 0;
    length = end - first;
  } else if (dx < -15) {
    const int available = dx + 15 + (int)Dkc1CoopFormationWord(ram, a + 6);
    if (available <= 0) return 0;
    length = ((unsigned)available >> 3) * 2;
    if (length > bytes) return 0;
    first = bytes - length;
  } else {
    length = ((263 - dx) >> 3) * 2;
    if (length > bytes) length = bytes;
  }
  /* BA2D..BA48. Preserve the two 16-bit wraps and ASL's carry: the frame
   * counter's high bits affect native phase on the wrap frame too. */
  unsigned clock = Dkc1CoopFormationWord(ram, 0x28) >> 1;
  unsigned phase_sum = ((clock * 4) & 0xffff) + fx + ((clock >> 14) & 1);
  phase_sum = ((phase_sum & 0xffff) + fy) & 0xffff;
  const uint16_t tile = (uint16_t)(((phase_sum >> 2) + first) & 0x0e) |
                        Dkc1CoopFormationWord(ram, 0xd1);
  unsigned count = 0;
  /* BA54 truncates the visible byte count to Y8. Columns are drawn right to
   * left, then the high and low bitmap bytes each enumerate rows top down. */
  for (int column = wide ? (int)length - 2 : (int8_t)(length - 2);
       column >= 0; column -= 2) {
    uint16_t bits = Dkc1CoopFormationWord(ram, bitmap + first + (unsigned)column) & mask;
    const int16_t sx = (int16_t)(dx + (int)first * 4 +
        (wide ? column * 4 : (int)Dkc1CoopFormationWord(bank, 0xbd9c + (unsigned)column)));
    for (unsigned half = 0; half < 2; half++) {
      unsigned rows = half ? bits & 255 : bits >> 8;
      for (unsigned guard = 0; rows && guard < 8; guard++) {
        const unsigned row = bank[(half ? 0xbfde : 0xbede) + rows];
        /* Native BD9C is an 8px grid, BD5C repeats its eight-frame phase.
         * Extend those sequences beyond the original 32-column lookup area
         * without reading its adjacent, unrelated ROM tables. */
        const unsigned offset = ((row >> 2) +
            (wide ? (unsigned)column : bank[0xbd5c + column])) & 0x0e;
        const uint16_t attributes = (tile & 0xff00) |
            (uint8_t)(((tile & 255) + offset) & 0xee);
        /* BA56 decrements $58, but SEP #$21 sets carry for the row ADC. */
        emit(context, sx, (uint8_t)(dy + (int)row), attributes);
        count++;
        rows = bank[0xbdde + rows];
      }
    }
  }
  return count;
}

unsigned Dkc1CoopFormationsDraw(const uint8_t *ram, const uint8_t *rom,
                              uint16_t camera_x, uint16_t camera_y,
                              Dkc1CoopFormationEmit emit, void *context) {
  return Dkc1CoopFormationsDrawWide(ram, rom, camera_x, camera_y, 0, 0,
                                  emit, context);
}

unsigned Dkc1CoopFormationsDrawWide(const uint8_t *ram, const uint8_t *rom,
                                  uint16_t camera_x, uint16_t camera_y,
                                  unsigned extra_left, unsigned extra_right,
                                  Dkc1CoopFormationEmit emit, void *context) {
  if (!ram || !rom || !emit || extra_left > 128 || extra_right > 128) return 0;
  unsigned count = 0;
  const uint16_t relative_x = (uint16_t)(camera_x - Dkc1CoopFormationWord(ram, 0x1b23));
  camera_y &= 0x3fff;
  for (unsigned a = 0xc000; a < 0xd000; a += 16) {
    Dkc1CoopFormationRecord record;
    int valid=Dkc1CoopFormationRead(ram,a,&record);
    if (valid<0) break;
    if (!valid || !(record.flags & Dkc1CoopFormationWord(ram,0xc4))) continue;
    count += DrawRecord(ram, rom, &record, relative_x, camera_y,
                        extra_left, extra_right, emit, context);
  }
  return count;
}
