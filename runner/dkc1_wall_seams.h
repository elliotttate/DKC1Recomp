#ifndef DKC1_WALL_SEAMS_H
#define DKC1_WALL_SEAMS_H

#include "dkc1_terrain.h"

/* A verified authored junction, not a general replacement of populated art.
 * The west column belongs to the neighboring corridor at rows 316..322.
 * The east wall repeats every three rows and joins the west column correctly
 * at rows 325..327. Validate the entire junction and that donor strip before
 * using its authentic metatile cells in an entirely offscreen west column.
 * Source and scrolling evidence: docs/WIDESCREEN_WALL_SEAM.md. */
static inline bool Dkc1WallSeamSourceMatches(Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[12][2] = {
    {0x40cb, 0x4040}, {0x4001, 0x00ca}, {0x40cb, 0x408c},
    {0x4008, 0x4040}, {0x0100, 0x00ca}, {0x0109, 0x408c},
    {0x0112, 0x4040}, {0x010a, 0x00ca}, {0x00fe, 0x408c},
    {0x0034, 0x4040}, {0x010a, 0x00ca}, {0x00fe, 0x408c},
  };
  if (!read) return false;
  for (unsigned y = 0; y < 12; y++) {
    for (unsigned x = 0; x < 2; x++) {
      uint16_t cell;
      if (!read(ctx, 28u + x, 316u + y, &cell) ||
          cell != junction[y][x]) return false;
    }
  }
  return true;
}

static inline bool Dkc1WallSeamDonorRow(
    uint32_t target_x, uint32_t target_y, uint32_t native_left_x,
    uint32_t *donor_y) {
  if (!donor_y || target_x != 28u || native_left_x != 29u ||
      target_y < 316u || target_y > 322u) return false;
  *donor_y = 325u + (target_y - 316u) % 3u;
  return true;
}

/* The opposite side of the same junction faces a different passage. Its
 * native wall is column 28, so the west-facing correction above cannot be
 * reused. These authored donor triples preserve each native edge cell and
 * supply its two matching interior cells. Both target and donor bytes are
 * checked, including the rows immediately above and below the correction. */
typedef struct Dkc1EastWallDonor {
  uint16_t x, y, edge, inner, outer;
} Dkc1EastWallDonor;
static const Dkc1EastWallDonor kDkc1EastWallDonors[10] = {
  {50, 307, 0x4001, 0x0040, 0x4040},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 306, 0x4008, 0x008c, 0x408c},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 307, 0x4001, 0x0040, 0x4040},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 306, 0x4008, 0x008c, 0x408c},
  {50, 310, 0x0100, 0x0040, 0x4040},
  {50, 311, 0x0109, 0x010a, 0x410a},
  {32, 300, 0x0112, 0x00fe, 0x40d9},
};

static inline bool Dkc1EastWallSeamSourceMatches(
    Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[12][3] = {
    {0x0007, 0x0000, 0x0000}, {0x4001, 0x0000, 0x0000},
    {0x40cb, 0x0000, 0x0000}, {0x4008, 0x0000, 0x0000},
    {0x40cb, 0x4040, 0x0001}, {0x4001, 0x00ca, 0x00cb},
    {0x40cb, 0x408c, 0x0008}, {0x4008, 0x4040, 0x0001},
    {0x0100, 0x00ca, 0x00cb}, {0x0109, 0x408c, 0x0008},
    {0x0112, 0x4040, 0x0001}, {0x010a, 0x00ca, 0x00cb},
  };
  if (!read) return false;
  for (unsigned y = 0; y < 12; y++) {
    for (unsigned x = 0; x < 3; x++) {
      uint16_t cell;
      if (!read(ctx, 28u + x, 312u + y, &cell) ||
          cell != junction[y][x]) return false;
    }
  }
  for (unsigned y = 0; y < 10; y++) {
    const Dkc1EastWallDonor *d = &kDkc1EastWallDonors[y];
    const uint16_t expected[3] = {d->edge, d->inner, d->outer};
    if (d->edge != junction[y + 1u][0]) return false;
    for (unsigned x = 0; x < 3; x++) {
      uint16_t cell;
      if (!read(ctx, d->x + x, d->y, &cell) ||
          cell != expected[x]) return false;
    }
  }
  return true;
}

static inline bool Dkc1EastWallSeamDonor(
    uint32_t target_x, uint32_t target_y, uint32_t native_right_x,
    uint32_t *donor_x, uint32_t *donor_y) {
  if (!donor_x || !donor_y || native_right_x != 28u ||
      target_x < 29u || target_x > 30u ||
      target_y < 313u || target_y > 322u) return false;
  const Dkc1EastWallDonor *d = &kDkc1EastWallDonors[target_y - 313u];
  *donor_x = d->x + target_x - 28u;
  *donor_y = d->y;
  return true;
}

#endif
