#include <stdio.h>
#include <string.h>
#include "dkc1_terrain.h"

static uint16_t cells[512][64];
static Dkc1TerrainFill fills[512][64];
static bool read_cell(void *ctx, uint32_t x, uint32_t y, uint16_t *id) {
  (void)ctx;
  if (x >= 64 || y >= 512) return false;
  *id = cells[y][x];
  return true;
}
static Dkc1TerrainFill fill(void *ctx, uint32_t x, uint32_t y) {
  (void)ctx;
  return x < 64 && y < 512 ? fills[y][x] : kDkc1TerrainUnknown;
}
#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "terrain check failed at line %d: %s\n", __LINE__, #c); \
  return 1; } } while (0)

int main(void) {
  for (unsigned y = 0; y < 512; y++)
    for (unsigned x = 0; x < 64; x++) fills[y][x] = kDkc1TerrainEmpty;
  for (unsigned y = 3; y <= 8; y++)
    fills[y][10] = fills[y][11] = kDkc1TerrainFull;
  uint32_t source = 0;
  CHECK(Dkc1TerrainWallSource(fill, NULL, true, 13, 11, 5, &source));
  CHECK(source == 11);
  CHECK(Dkc1TerrainWallSource(fill, NULL, false, 8, 10, 5, &source));
  CHECK(source == 10);
  fills[5][12] = kDkc1TerrainPartial;
  CHECK(!Dkc1TerrainWallSource(fill, NULL, true, 13, 11, 5, &source));
  fills[5][12] = kDkc1TerrainEmpty;
  fills[5][10] = kDkc1TerrainEmpty;
  CHECK(!Dkc1TerrainWallSource(fill, NULL, true, 13, 11, 5, &source));
  fills[5][10] = kDkc1TerrainFull;
  fills[4][10] = fills[6][10] = kDkc1TerrainEmpty;
  CHECK(!Dkc1TerrainWallSource(fill, NULL, true, 13, 11, 5, &source));
  fills[4][10] = fills[6][10] = kDkc1TerrainFull;
  CHECK(!Dkc1TerrainWallSource(fill, NULL, true, 64, 11, 5, &source));
  CHECK(!Dkc1TerrainWallSource(fill, NULL, true, 13, 11, 512, &source));

  /* A ceiling above a six-row sealed shaft does not turn the shaft into a
   * doorway. A two-row opening and open water above a single crate do. */
  fills[2][13] = fills[9][13] = kDkc1TerrainFull;
  CHECK(Dkc1TerrainSealedVoid(fill, NULL, true, 13, 11, 5, 2));
  fills[4][13] = fills[7][13] = kDkc1TerrainFull;
  CHECK(!Dkc1TerrainSealedVoid(fill, NULL, true, 13, 11, 5, 2));
  fills[4][13] = fills[7][13] = kDkc1TerrainEmpty;
  for (unsigned y = 3; y <= 8; y++)
    if (y != 5) fills[y][11] = kDkc1TerrainEmpty;
  CHECK(!Dkc1TerrainSealedVoid(fill, NULL, true, 13, 11, 5, 2));

  /* Repeated authored panel -> rock -> rock2 pairs prefer rock over copying
   * the panel. Direction, orientation, ties, and unavailable chains matter. */
  memset(cells, 0, sizeof cells);
  for (unsigned y = 0; y < 4; y++) {
    cells[y][0] = 1; cells[y][1] = 2; cells[y][2] = 3;
    fills[y][0] = fills[y][1] = fills[y][2] = kDkc1TerrainFull;
  }
  cells[0][1] = 1; /* A single self-neighbor loses to three authored fills. */
  Dkc1TerrainAdjacency map;
  CHECK(Dkc1TerrainBuildAdjacency(&map, read_cell, fill, NULL, 0, 2, 0, 3));
  uint16_t next = 0;
  CHECK(Dkc1TerrainSuccessor(&map, 1, true, &next) && next == 2);
  CHECK(Dkc1TerrainContinue(&map, 1, true, 2, &next) && next == 3);
  CHECK(Dkc1TerrainSuccessor(&map, 2, false, &next) && next == 1);
  CHECK(!Dkc1TerrainSuccessor(&map, 0x4001, true, &next));
  CHECK(!Dkc1TerrainContinue(&map, 3, true, 1, &next));
  cells[2][1] = cells[3][1] = 4;
  CHECK(Dkc1TerrainBuildAdjacency(&map, read_cell, fill, NULL, 0, 1, 1, 2));
  CHECK(!Dkc1TerrainSuccessor(&map, 1, true, &next));
  CHECK(!Dkc1TerrainBuildAdjacency(&map, read_cell, fill, NULL, 0, 64, 0, 2));
  CHECK(!map.valid);
  CHECK(!Dkc1TerrainBuildAdjacency(&map, read_cell, fill, NULL, 0, 2, 0, 8));
  CHECK(!Dkc1TerrainSuccessor(&map, 1, true, &next));
  puts("terrain adjacency and opening preservation: PASS");
  return 0;
}
