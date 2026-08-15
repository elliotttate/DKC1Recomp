#ifndef DKC1_WRAM_DUMP_H
#define DKC1_WRAM_DUMP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { kDkc1WramDumpMaxRanges = 32 };

typedef struct Dkc1WramDumpRange {
  uint32_t first;
  uint32_t last;
} Dkc1WramDumpRange;

typedef struct Dkc1WramDump {
  long first_frame;
  long last_frame;
  Dkc1WramDumpRange ranges[kDkc1WramDumpMaxRanges];
  size_t range_count;
  size_t payload_size;
  uint64_t raw_offset;
  FILE *raw;
  FILE *index;
  uint8_t *payload;
  char raw_path[768];
  char index_path[800];
} Dkc1WramDump;

/* Configure from DKC1_WRAM_DUMP=<first>-<last>,
 * DKC1_WRAM_DUMP_PATH=<raw.bin>, and optional hexadecimal inclusive
 * DKC1_WRAM_DUMP_RANGES=<first>-<last>[,...]. Returns 1 when armed, 0 when
 * disabled, and -1 on validation/open failure. */
int Dkc1WramDumpOpenFromEnvironment(Dkc1WramDump *dump,
                                    char *error, size_t error_size);
int Dkc1WramDumpFrame(Dkc1WramDump *dump, long relative_frame,
                      int emulator_frame, const uint8_t *wram,
                      char *error, size_t error_size);
int Dkc1WramDumpClose(Dkc1WramDump *dump,
                      char *error, size_t error_size);

#endif
