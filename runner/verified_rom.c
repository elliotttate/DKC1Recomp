#include "verified_rom.h"

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DKC1 USA v1.0 headerless:
 * fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15 */
static const uint8_t kSupportedSha256[32] = {
  0xfa, 0x8c, 0xac, 0xf5, 0xbb, 0xfc, 0x39, 0xee,
  0x6b, 0xba, 0xa5, 0x57, 0xad, 0xf8, 0x91, 0x33,
  0xd6, 0x0d, 0x42, 0xf6, 0xcf, 0x9e, 0x1d, 0xb3,
  0x0d, 0x5a, 0x36, 0xa4, 0x69, 0xf7, 0x4d, 0x15,
};

static void SetError(char *error, size_t error_size, const char *message) {
  if (!error || error_size == 0) return;
  (void)snprintf(error, error_size, "%s", message);
}

static void SetUnsupportedError(char *error, size_t error_size, size_t size,
                                const uint8_t hash[32]) {
  if (!error || error_size == 0) return;
  int written = snprintf(error, error_size,
                         "unsupported ROM (size=%zu sha256=", size);
  if (written < 0 || (size_t)written >= error_size) return;
  size_t used = (size_t)written;
  for (size_t i = 0; i < 32 && used + 2 < error_size; i++) {
    written = snprintf(error + used, error_size - used, "%02x", hash[i]);
    if (written != 2) return;
    used += 2;
  }
  if (used + 2 <= error_size) {
    error[used++] = ')';
    error[used] = '\0';
  }
}

uint8_t *Dkc1ReadVerifiedRom(const char *path, size_t *size_out,
                             char *error, size_t error_size) {
  if (size_out) *size_out = 0;
  if (!path || !*path || !size_out) {
    SetError(error, error_size, "invalid ROM path or output pointer");
    return NULL;
  }

  FILE *stream = fopen(path, "rb");
  if (!stream) {
    SetError(error, error_size, "unable to open ROM");
    return NULL;
  }
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    SetError(error, error_size, "unable to seek ROM");
    return NULL;
  }
  long length = ftell(stream);
  if (length <= 0 || fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    SetError(error, error_size, "ROM is empty or unreadable");
    return NULL;
  }

  uint8_t *file = (uint8_t *)malloc((size_t)length);
  if (!file) {
    fclose(stream);
    SetError(error, error_size, "not enough memory to load ROM");
    return NULL;
  }
  if (fread(file, 1, (size_t)length, stream) != (size_t)length) {
    free(file);
    fclose(stream);
    SetError(error, error_size, "unable to read complete ROM");
    return NULL;
  }
  if (fclose(stream) != 0) {
    free(file);
    SetError(error, error_size, "unable to close ROM after reading");
    return NULL;
  }

  size_t skip = ((size_t)length % 1024u == 512u) ? 512u : 0u;
  size_t payload_size = (size_t)length - skip;
  if (skip) memmove(file, file + skip, payload_size);

  uint8_t hash[32];
  sha256_compute(file, payload_size, hash);
  if (payload_size != 0x400000u ||
      memcmp(hash, kSupportedSha256, sizeof hash) != 0) {
    SetUnsupportedError(error, error_size, payload_size, hash);
    free(file);
    return NULL;
  }

  *size_out = payload_size;
  if (error && error_size) error[0] = '\0';
  return file;
}
