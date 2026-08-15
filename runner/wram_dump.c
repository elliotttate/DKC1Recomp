#include "wram_dump.h"

#include "sha256.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { kWramSize = 0x20000 };

static void SetError(char *error, size_t size, const char *message) {
  if (error && size)
    (void)snprintf(error, size, "%s", message);
}

static int ParseNumber(const char *text, int base, unsigned long maximum,
                       const char **end_out, unsigned long *value) {
  if (!text || !*text || *text == '+' || *text == '-' ||
      *text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    return 0;
  errno = 0;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, base);
  if (errno || end == text || parsed > maximum)
    return 0;
  *end_out = end;
  *value = parsed;
  return 1;
}

static int ParseFrameRange(const char *text, long *first, long *last) {
  const char *end = NULL;
  unsigned long a = 0, b = 0;
  if (!text || !ParseNumber(text, 10, 1000000ul, &end, &a) ||
      *end != '-' || !ParseNumber(end + 1, 10, 1000000ul, &end, &b) ||
      *end || a < 1 || b < a)
    return 0;
  *first = (long)a;
  *last = (long)b;
  return 1;
}

static int ParseRanges(Dkc1WramDump *dump, const char *text) {
  if (!text || !*text)
    text = "00000-1ffff";
  while (*text) {
    if (dump->range_count >= kDkc1WramDumpMaxRanges)
      return 0;
    const char *end = NULL;
    unsigned long first = 0, last = 0;
    if (!ParseNumber(text, 16, kWramSize - 1, &end, &first) ||
        *end != '-' ||
        !ParseNumber(end + 1, 16, kWramSize - 1, &end, &last) ||
        last < first || (*end && *end != ','))
      return 0;
    if (dump->range_count &&
        first <= dump->ranges[dump->range_count - 1].last)
      return 0;  /* sorted, non-overlapping ranges make replay unambiguous */
    dump->ranges[dump->range_count].first = (uint32_t)first;
    dump->ranges[dump->range_count].last = (uint32_t)last;
    dump->range_count++;
    dump->payload_size += (size_t)(last - first + 1);
    text = *end ? end + 1 : end;
  }
  return dump->range_count != 0 && dump->payload_size <= kWramSize;
}

static void PrintHash(FILE *stream, const uint8_t hash[32]) {
  for (int i = 0; i < 32; i++)
    (void)fprintf(stream, "%02x", hash[i]);
}

int Dkc1WramDumpOpenFromEnvironment(Dkc1WramDump *dump,
                                    char *error, size_t error_size) {
  memset(dump, 0, sizeof *dump);
  const char *frames = getenv("DKC1_WRAM_DUMP");
  if (!frames || !*frames)
    return 0;
  const char *path = getenv("DKC1_WRAM_DUMP_PATH");
  if (!ParseFrameRange(frames, &dump->first_frame, &dump->last_frame)) {
    SetError(error, error_size,
             "DKC1_WRAM_DUMP must be inclusive relative frames first-last");
    return -1;
  }
  if (!path || !*path || strlen(path) >= sizeof dump->raw_path) {
    SetError(error, error_size,
             "DKC1_WRAM_DUMP_PATH is required and is too long or empty");
    return -1;
  }
  if (!ParseRanges(dump, getenv("DKC1_WRAM_DUMP_RANGES"))) {
    SetError(error, error_size,
             "DKC1_WRAM_DUMP_RANGES must be sorted non-overlapping hex ranges");
    return -1;
  }
  (void)snprintf(dump->raw_path, sizeof dump->raw_path, "%s", path);
  if ((size_t)snprintf(dump->index_path, sizeof dump->index_path,
                       "%s.jsonl", path) >= sizeof dump->index_path) {
    SetError(error, error_size, "WRAM dump index path is too long");
    return -1;
  }
  dump->payload = (uint8_t *)malloc(dump->payload_size);
  dump->raw = fopen(dump->raw_path, "wb");
  dump->index = fopen(dump->index_path, "wb");
  if (!dump->payload || !dump->raw || !dump->index) {
    SetError(error, error_size, "unable to allocate/open WRAM dump outputs");
    (void)Dkc1WramDumpClose(dump, NULL, 0);
    return -1;
  }
  (void)fprintf(dump->index,
                "{\"schema\":\"dkc1.wram.dump.v1\",\"type\":\"manifest\","
                "\"first_frame\":%ld,\"last_frame\":%ld,"
                "\"payload_size\":%zu,\"ranges\":[",
                dump->first_frame, dump->last_frame, dump->payload_size);
  for (size_t i = 0; i < dump->range_count; i++)
    (void)fprintf(dump->index, "%s[\"%05x\",\"%05x\"]",
                  i ? "," : "", dump->ranges[i].first,
                  dump->ranges[i].last);
  (void)fprintf(dump->index, "]}\n");
  return 1;
}

int Dkc1WramDumpFrame(Dkc1WramDump *dump, long relative_frame,
                      int emulator_frame, const uint8_t *wram,
                      char *error, size_t error_size) {
  if (!dump->raw || relative_frame < dump->first_frame ||
      relative_frame > dump->last_frame)
    return 1;
  size_t position = 0;
  for (size_t i = 0; i < dump->range_count; i++) {
    size_t length = dump->ranges[i].last - dump->ranges[i].first + 1u;
    memcpy(dump->payload + position, wram + dump->ranges[i].first, length);
    position += length;
  }
  uint8_t hash[32];
  sha256_compute(dump->payload, dump->payload_size, hash);
  if (fwrite(dump->payload, 1, dump->payload_size, dump->raw) !=
      dump->payload_size) {
    SetError(error, error_size, "unable to write WRAM dump payload");
    return 0;
  }
  (void)fprintf(dump->index,
                "{\"schema\":\"dkc1.wram.dump.v1\",\"type\":\"frame\","
                "\"relative_frame\":%ld,\"emulator_frame\":%d,"
                "\"offset\":%llu,\"length\":%zu,\"sha256\":\"",
                relative_frame, emulator_frame,
                (unsigned long long)dump->raw_offset, dump->payload_size);
  PrintHash(dump->index, hash);
  if (fprintf(dump->index, "\"}\n") < 0 || ferror(dump->index)) {
    SetError(error, error_size, "unable to write WRAM dump index");
    return 0;
  }
  dump->raw_offset += dump->payload_size;
  return 1;
}

int Dkc1WramDumpClose(Dkc1WramDump *dump,
                      char *error, size_t error_size) {
  int ok = 1;
  if (dump->raw && fclose(dump->raw) != 0) ok = 0;
  if (dump->index && fclose(dump->index) != 0) ok = 0;
  free(dump->payload);
  dump->raw = NULL;
  dump->index = NULL;
  dump->payload = NULL;
  if (!ok) SetError(error, error_size, "unable to close WRAM dump outputs");
  return ok;
}
