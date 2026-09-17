#pragma once

#include <stddef.h>
#include <stdint.h>

/* Loads the caller-owned private ROM, removes an optional 512-byte copier
 * header, and accepts the supported headerless USA v1.0 payload. Development
 * tools may opt into one exact 4 MB modified payload by setting
 * DKC1_ALLOW_ROM_SHA256 to its complete 64-character digest. The returned
 * buffer belongs to the caller and must be released with free(). */
uint8_t *Dkc1ReadVerifiedRom(const char *path, size_t *size_out,
                             char *error, size_t error_size);
