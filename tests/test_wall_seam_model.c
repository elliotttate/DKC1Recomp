#include <stdio.h>
#include <string.h>
#include "dkc1_wall_seams.h"

/* Fixture read directly from the supported ROM's E9:9E38..E9:A3BA
 * two-column wall junction. A changed cell must invalidate the capability. */
static uint16_t cells[12][2] = {
  {0x40cb,0x4040},{0x4001,0x00ca},{0x40cb,0x408c},
  {0x4008,0x4040},{0x0100,0x00ca},{0x0109,0x408c},
  {0x0112,0x4040},{0x010a,0x00ca},{0x00fe,0x408c},
  {0x0034,0x4040},{0x010a,0x00ca},{0x00fe,0x408c},
};
static bool read_cell(void *ctx,uint32_t x,uint32_t y,uint16_t *cell) {
  if (ctx || x<28 || x>29 || y<316 || y>327) return false;
  *cell=cells[y-316][x-28];return true;
}
/* Independent target and donor records from the right-edge reproduction. */
static const uint16_t east_junction[12][3] = {
  {0x0007,0x0000,0x0000},{0x4001,0x0000,0x0000},
  {0x40cb,0x0000,0x0000},{0x4008,0x0000,0x0000},
  {0x40cb,0x4040,0x0001},{0x4001,0x00ca,0x00cb},
  {0x40cb,0x408c,0x0008},{0x4008,0x4040,0x0001},
  {0x0100,0x00ca,0x00cb},{0x0109,0x408c,0x0008},
  {0x0112,0x4040,0x0001},{0x010a,0x00ca,0x00cb},
};
static const uint16_t donor_records[6][5] = {
  {50,306,0x4008,0x008c,0x408c},
  {50,307,0x4001,0x0040,0x4040},
  {50,308,0x40cb,0x40ca,0x00ca},
  {50,310,0x0100,0x0040,0x4040},
  {50,311,0x0109,0x010a,0x410a},
  {32,300,0x0112,0x00fe,0x40d9},
};
static uint16_t east_map[512][64];
static bool east_valid[512][64];
static bool read_east(void *ctx,uint32_t x,uint32_t y,uint16_t *cell) {
  if (ctx || x>=64 || y>=512 || !east_valid[y][x]) return false;
  *cell=east_map[y][x];return true;
}
#define CHECK(c) do {if (!(c)) {fprintf(stderr,"wall seam check line %d: %s\n",__LINE__,#c);return 1;}} while (0)
int main(void) {
  CHECK(Dkc1WallSeamSourceMatches(read_cell,NULL));
  CHECK(!Dkc1WallSeamSourceMatches(NULL,NULL));
  CHECK(!Dkc1WallSeamSourceMatches(read_cell,(void *)1));
  for (unsigned y=0;y<12;y++) for (unsigned x=0;x<2;x++) {
    cells[y][x]^=0x4000;CHECK(!Dkc1WallSeamSourceMatches(read_cell,NULL));cells[y][x]^=0x4000;
  }
  for (uint32_t y=316;y<=322;y++) {
    uint32_t donor=0;
    CHECK(Dkc1WallSeamDonorRow(28,y,29,&donor));
    CHECK(donor>=325 && donor<=327);
    CHECK(cells[y-316][1]==cells[donor-316][1]); // same phase of the native wall
    CHECK(!Dkc1WallSeamDonorRow(28,y,28,&donor)); // native pixels never eligible
    CHECK(!Dkc1WallSeamDonorRow(28,y,30,&donor)); // a different corridor is not eligible
    CHECK(!Dkc1WallSeamDonorRow(27,y,29,&donor));
    CHECK(!Dkc1WallSeamDonorRow(29,y,29,&donor));
  }
  uint32_t donor=99;
  CHECK(!Dkc1WallSeamDonorRow(28,315,29,&donor) && donor==99);
  CHECK(!Dkc1WallSeamDonorRow(28,323,29,&donor) && donor==99);
  CHECK(!Dkc1WallSeamDonorRow(28,316,29,NULL));
  for (unsigned y=0;y<12;y++) for (unsigned x=0;x<3;x++) {
    east_map[312+y][28+x]=east_junction[y][x];east_valid[312+y][28+x]=true;
  }
  for (unsigned i=0;i<6;i++) for (unsigned x=0;x<3;x++) {
    const uint16_t *r=donor_records[i];
    east_map[r[1]][r[0]+x]=r[2+x];east_valid[r[1]][r[0]+x]=true;
  }
  CHECK(Dkc1EastWallSeamSourceMatches(read_east,NULL));
  CHECK(!Dkc1EastWallSeamSourceMatches(NULL,NULL));
  CHECK(!Dkc1EastWallSeamSourceMatches(read_east,(void *)1));
  for (unsigned y=0;y<512;y++) for (unsigned x=0;x<64;x++) {
    if (!east_valid[y][x]) continue;
    east_map[y][x]^=0x4000;
    CHECK(!Dkc1EastWallSeamSourceMatches(read_east,NULL));
    east_map[y][x]^=0x4000;east_valid[y][x]=false;
    CHECK(!Dkc1EastWallSeamSourceMatches(read_east,NULL));
    east_valid[y][x]=true;
  }
  for (uint32_t y=313;y<=322;y++) for (uint32_t x=29;x<=30;x++) {
    uint32_t dx=0,dy=0;
    CHECK(Dkc1EastWallSeamDonor(x,y,28,&dx,&dy));
    CHECK(east_valid[dy][dx]);
    CHECK(east_map[dy][dx-(x-28)]==east_map[y][28]);
    CHECK(!Dkc1EastWallSeamDonor(x,y,29,&dx,&dy));
    CHECK(!Dkc1EastWallSeamDonor(x,y,27,&dx,&dy));
  }
  uint32_t dx=99,dy=99;
  CHECK(!Dkc1EastWallSeamDonor(28,314,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(31,314,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,312,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,323,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,314,28,NULL,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,314,28,&dx,NULL));
  CHECK(dx==99 && dy==99);
  puts("wall seam source and native containment: PASS");return 0;
}
