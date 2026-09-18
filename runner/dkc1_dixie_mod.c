#include "dkc1_dixie_mod.h"

#include "verified_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional "Dixie Kong Country" mod switch (see dkc1_dixie_mod.h).
 *
 * The mod needs no external patched ROM: the variant build synthesizes the
 * modded 6 MiB image in memory from the user's verified clean DKC1 ROM plus
 * the embedded IPS patch (runner/dkc1_dixie_patch.inc), and refuses to run
 * unless the synthesized image hashes to the pinned mod identity. The stock
 * build never touches the patch; with the persisted setting on it simply
 * hands the session to the sibling variant executable.
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <unistd.h>
extern char **environ;
#endif

#include "dkc1_dixie_patch.inc"
#include "sha256.h"

/* Pinned identities (runner/verified_rom.c holds the same table). */
static const size_t kCleanRomSize = 0x400000;
static const size_t kModRomSize = 0x600000;
static char s_rom_path[4096];

void Dkc1DixieSetRomPath(const char *path) {
  snprintf(s_rom_path, sizeof s_rom_path, "%s", path ? path : "");
}

/* sha256 of the pinned modded image (see docs/DIXIE_HD_MAC.md). */
static const uint8_t kModSha256[32] = {
  0x27, 0x69, 0xb7, 0x2a, 0x8a, 0x20, 0x50, 0x00,
  0x03, 0x36, 0xf5, 0xdd, 0x6d, 0xea, 0x1a, 0x45,
  0x38, 0x5f, 0x4f, 0x35, 0xee, 0x71, 0x0d, 0xaf,
  0xb0, 0xc0, 0xa3, 0x59, 0x22, 0x95, 0x64, 0x3b,
};

static int Dkc1DixieMemoryMatchesModIdentity(const uint8_t *image,
                                             size_t size) {
  if (!image || size != kModRomSize) return 0;
  uint8_t digest[32];
  sha256_compute(image, size, digest);
  return memcmp(digest, kModSha256, sizeof digest) == 0;
}

int Dkc1DixieIsVariant(void) {
#ifdef DKC1_DIXIE_VARIANT
  return 1;
#else
  return 0;
#endif
}

/* Applies an IPS patch image to `buf` in place. Returns 1 on success, 0 on
 * malformed input (with `error` set). */
static int ApplyIps(const unsigned char *ips, unsigned long ips_size,
                    unsigned char *buf, unsigned long buf_size,
                    char *error, size_t error_size) {
  if (error && error_size) error[0] = '\0';
  if (!ips || ips_size < 8 || memcmp(ips, "PATCH", 5) != 0) {
    snprintf(error, error_size, "malformed embedded mod patch");
    return 0;
  }
  unsigned long pos = 5;
  while (pos + 3 <= ips_size) {
    if (memcmp(ips + pos, "EOF", 3) == 0) return 1;
    unsigned long off =
        ((unsigned long)ips[pos] << 16) | ((unsigned long)ips[pos + 1] << 8) |
        ips[pos + 2];
    pos += 3;
    if (pos + 2 > ips_size) break;
    unsigned len = ((unsigned)ips[pos] << 8) | ips[pos + 1];
    pos += 2;
    if (len == 0) {
      /* RLE: 16-bit repeat count + fill byte. */
      if (pos + 3 > ips_size) break;
      unsigned count = ((unsigned)ips[pos] << 8) | ips[pos + 1];
      unsigned char value = ips[pos + 2];
      pos += 3;
      if (off + count > buf_size) {
        snprintf(error, error_size, "mod patch range out of bounds");
        return 0;
      }
      memset(buf + off, value, count);
    } else {
      if (pos + len > ips_size || off + len > buf_size) {
        snprintf(error, error_size, "mod patch range out of bounds");
        return 0;
      }
      memcpy(buf + off, ips + pos, len);
      pos += len;
    }
  }
  snprintf(error, error_size, "unterminated embedded mod patch");
  return 0;
}

/* Loads the mod ROM image for the variant build: accepts either the pinned
 * clean ROM (synthesizes the modded image) or an already-patched ROM (passed
 * through). Returns a malloc-owned buffer of 0x600000 bytes. */
uint8_t *Dkc1DixieLoadRom(const char *path, size_t *size_out, char *error,
                          size_t error_size) {
  if (size_out) *size_out = 0;
  if (error && error_size) error[0] = '\0';
  if (!path || !*path || !size_out) {
    snprintf(error, error_size, "invalid ROM path");
    return NULL;
  }

  FILE *stream = fopen(path, "rb");
  if (!stream) {
    snprintf(error, error_size, "unable to open ROM");
    return NULL;
  }
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    snprintf(error, error_size, "unable to seek ROM");
    return NULL;
  }
  long length = ftell(stream);
  if (length <= 0 || fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    snprintf(error, error_size, "ROM is empty or unreadable");
    return NULL;
  }
  uint8_t *file = (uint8_t *)malloc((size_t)length);
  if (!file) {
    fclose(stream);
    snprintf(error, error_size, "not enough memory to load ROM");
    return NULL;
  }
  if (fread(file, 1, (size_t)length, stream) != (size_t)length) {
    free(file);
    fclose(stream);
    snprintf(error, error_size, "unable to read complete ROM");
    return NULL;
  }
  fclose(stream);

  size_t skip = ((size_t)length % 1024u == 512u) ? 512u : 0u;
  size_t payload = (size_t)length - skip;
  uint8_t *image;

  if (payload == kModRomSize) {
    /* Already the modded image (e.g. legacy workflow): use it directly. */
    image = (uint8_t *)calloc(1, kModRomSize);
    if (!image) {
      free(file);
      snprintf(error, error_size, "not enough memory for mod ROM");
      return NULL;
    }
    memcpy(image, file + skip, kModRomSize);
    free(file);
  } else if (payload == kCleanRomSize) {
    /* Clean ROM: verify it, then synthesize the modded image. */
    uint8_t *verified = Dkc1ReadVerifiedRom(path, size_out, error, error_size);
    if (!verified) {
      free(file);
      return NULL; /* error already describes the mismatch */
    }
    free(verified);
    image = (uint8_t *)calloc(1, kModRomSize);
    if (!image) {
      free(file);
      snprintf(error, error_size, "not enough memory for mod ROM");
      return NULL;
    }
    memcpy(image, file + skip, kCleanRomSize);
    free(file);
    char apply_error[96] = {0};
    if (!ApplyIps(kDkc1DixiePatchIps, kDkc1DixiePatchIpsSize, image,
                  kModRomSize, apply_error, sizeof apply_error)) {
      free(image);
      snprintf(error, error_size, "mod synthesis failed: %s",
               apply_error[0] ? apply_error : "unknown");
      return NULL;
    }
  } else {
    free(file);
    snprintf(error, error_size,
             "unsupported ROM size %zu (expected the clean or modded image)",
             payload);
    return NULL;
  }

  /* The synthesized image must exactly equal the pinned mod identity. */
  if (!Dkc1DixieMemoryMatchesModIdentity(image, kModRomSize)) {
    free(image);
    if (payload == kCleanRomSize)
      snprintf(error, error_size,
               "mod synthesis produced an unexpected image");
    else
      snprintf(error, error_size, "unsupported mod ROM identity");
    return NULL;
  }

  *size_out = kModRomSize;
  return image;
}

#if defined(_WIN32)
static char *RegReadPath(const wchar_t *value) {
  wchar_t buf[4096];
  DWORD type = 0, size = sizeof buf;
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0,
                    KEY_READ, &key) != ERROR_SUCCESS)
    return NULL;
  LSTATUS st = RegQueryValueExW(key, value, NULL, &type, (BYTE *)buf, &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t))
    return NULL;
  buf[(size / sizeof(wchar_t)) - 1] = L'\0';
  int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
  if (n <= 0) return NULL;
  char *out = (char *)malloc((size_t)n);
  if (!out) return NULL;
  WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, n, NULL, NULL);
  return out;
}

static void RegWritePath(const wchar_t *value, const char *path) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0,
                      NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                      NULL) != ERROR_SUCCESS)
    return;
  if (path) {
    wchar_t buf[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, buf, 4096);
    RegSetValueExW(key, value, 0, REG_SZ, (const BYTE *)buf,
                   (DWORD)((wcslen(buf) + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, value);
  }
  RegCloseKey(key);
}

static int RegReadInt(const wchar_t *value, int fallback) {
  HKEY key;
  DWORD type = 0, data = 0, size = sizeof data;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0,
                    KEY_READ, &key) != ERROR_SUCCESS)
    return fallback;
  LSTATUS st = RegQueryValueExW(key, value, NULL, &type, (BYTE *)&data, &size);
  RegCloseKey(key);
  return (st == ERROR_SUCCESS && type == REG_DWORD) ? (int)data : fallback;
}

static void RegWriteInt(const wchar_t *value, int data) {
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DKC1Recomp\\Mods", 0,
                      NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                      NULL) != ERROR_SUCCESS)
    return;
  DWORD v = (DWORD)data;
  RegSetValueExW(key, value, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
  RegCloseKey(key);
}
#endif /* _WIN32 */

int Dkc1DixieSavedEnabled(void) {
#if defined(_WIN32)
  return RegReadInt(L"Dixie", 0) != 0;
#elif defined(__APPLE__)
  const char *env = getenv("DKC1_DIXIE");
  if (env && env[0] && env[1] == '\0') return env[0] != '0';
  CFPropertyListRef value = CFPreferencesCopyAppValue(
      CFSTR("DixieKongCountryEnabled"),
      CFSTR("com.flat2vr.dkc1recomp.hdexperiment"));
  int enabled = value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
                CFBooleanGetValue((CFBooleanRef)value);
  if (value) CFRelease(value);
  return enabled;
#else
  const char *env = getenv("DKC1_DIXIE");
  return env && env[0] && env[0] != '0';
#endif
}

void Dkc1DixieSetEnabled(int enabled) {
#if defined(_WIN32)
  RegWriteInt(L"Dixie", enabled != 0);
  /* The mod ROM is synthesized now; drop any legacy saved path. */
  RegWritePath(L"DixieRom", NULL);
#elif defined(__APPLE__)
  CFPreferencesSetAppValue(
      CFSTR("DixieKongCountryEnabled"),
      enabled ? kCFBooleanTrue : kCFBooleanFalse,
      CFSTR("com.flat2vr.dkc1recomp.hdexperiment"));
  CFPreferencesAppSynchronize(
      CFSTR("com.flat2vr.dkc1recomp.hdexperiment"));
#else
  (void)enabled;
#endif
}

#if defined(_WIN32)
/* Spawns `exe` (same directory as the current executable) with the given
 * command-line tail, detached. Returns the new process id, or 0 on failure. */
static DWORD SpawnSibling(const char *exe, const char *tail) {
  wchar_t self[MAX_PATH];
  if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return 0;
  wchar_t *slash = wcsrchr(self, L'\\');
  if (!slash) return 0;
  slash[1] = L'\0';

  wchar_t target[MAX_PATH], wide_tail[8192], cmd[16384];
  if (_snwprintf_s(target, sizeof target / sizeof target[0], _TRUNCATE,
                  L"%s%S", self, exe) < 0 ||
      !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tail ? tail : "",
                          -1, wide_tail, sizeof wide_tail / sizeof wide_tail[0]) ||
      _snwprintf_s(cmd, sizeof cmd / sizeof cmd[0], _TRUNCATE,
                  L"\"%s\" %s", target, wide_tail) < 0)
    return 0;
  STARTUPINFOW si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(target, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return 0;
  DWORD pid = pi.dwProcessId;
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return pid;
}

/* Builds a quoted tail from argv[1..], replacing argv[1] with `rom` when it
 * is non-NULL. */
static void QuoteTail(char *out, size_t out_size, int argc, char **argv,
                      const char *rom) {
  out[0] = '\0';
  size_t used = 0;
  const char *first = rom ? rom : (argc > 1 ? argv[1] : NULL);
  if (first) {
    used += (size_t)_snprintf_s(out + used, out_size - used, _TRUNCATE,
                                "\"%s\"", first);
  }
  for (int i = 2; i < argc; i++) {
    if (used + 3 >= out_size) break;
    used += (size_t)_snprintf_s(out + used, out_size - used, _TRUNCATE,
                                " \"%s\"", argv[i]);
  }
}
#endif /* _WIN32 */

#if defined(__APPLE__)
/* Both Mac runtimes live in the same app bundle's Contents/MacOS directory.
 * Launching by absolute executable path keeps one distributable app while
 * retaining fully isolated recompilation globals in separate processes. */
static int SpawnMacSibling(const char *exe, const char *rom) {
  uint32_t size = 0;
  (void)_NSGetExecutablePath(NULL, &size);
  char *self = (char *)malloc(size ? size : 1u);
  if (!self || _NSGetExecutablePath(self, &size) != 0) {
    free(self);
    return 0;
  }
  char *slash = strrchr(self, '/');
  if (!slash) {
    free(self);
    return 0;
  }
  slash[1] = '\0';
  size_t target_size = strlen(self) + strlen(exe) + 1u;
  char *target = (char *)malloc(target_size);
  if (!target) {
    free(self);
    return 0;
  }
  snprintf(target, target_size, "%s%s", self, exe);
  free(self);
  char *const child_argv[] = {target, (char *)(rom ? rom : ""), NULL};
  pid_t child = 0;
  int result = access(target, X_OK) == 0
                   ? posix_spawn(&child, target, NULL, NULL, child_argv, environ)
                   : -1;
  free(target);
  return result == 0 && child > 0;
}
#endif

int Dkc1DixieHandoffCheck(int argc, char **argv, const char *variant_exe,
                          char *note, size_t note_size) {
  if (note && note_size) note[0] = '\0';
  if (argc > 1) Dkc1DixieSetRomPath(argv[1]);
#if defined(_WIN32)
  if (Dkc1DixieIsVariant()) return 0; /* variant never auto-hands-off */
  const char *env = getenv("DKC1_DIXIE");
  if (env && env[0] && env[0] == '0') return 0;
  if (!Dkc1DixieSavedEnabled() &&
      !(env && env[0] && env[1] == '\0' && env[0] == '1'))
    return 0;

  /* The sibling synthesizes the mod ROM from the clean ROM argument; an
   * explicit DKC1_DIXIE_ROM override replaces it (automation aid). */
  const char *rom = getenv("DKC1_DIXIE_ROM");
  if (!rom || !*rom) rom = s_rom_path;
  if (!*rom) return 0; /* A picker may still need to resolve the ROM. */

  char tail[8192];
  QuoteTail(tail, sizeof tail, argc, argv, rom);
  if (!SpawnSibling(variant_exe, tail)) {
    Dkc1DixieSetEnabled(0);
    if (note && note_size)
      snprintf(note, note_size,
               "Dixie Kong Country disabled: %s not found next to this "
               "executable", variant_exe);
    return 0;
  }
  return 1; /* caller must exit; the sibling owns the session now */
#elif defined(__APPLE__)
  if (Dkc1DixieIsVariant()) return 0;
  const char *env = getenv("DKC1_DIXIE");
  if (env && env[0] == '0' && env[1] == '\0') return 0;
  if (!Dkc1DixieSavedEnabled()) return 0;
  const char *rom = getenv("DKC1_DIXIE_ROM");
  if (!rom || !*rom) rom = s_rom_path;
  if (!*rom) return 0;
  setenv("DKC1_DIXIE", "1", 1);
  unsetenv("DKC1_USER_DIR");
  unsetenv("DKC1_SAVESTATE_INPUT");
  unsetenv("DKC1_PAUSE_AFTER_FRAME");
  if (!SpawnMacSibling(variant_exe, rom)) {
    Dkc1DixieSetEnabled(0);
    if (note && note_size)
      snprintf(note, note_size,
               "Dixie Kong Country disabled: %s is missing from the app",
               variant_exe);
    return 0;
  }
  return 1;
#else
  (void)argc; (void)argv; (void)variant_exe;
  if (note && note_size) note[0] = '\0';
  return 0;
#endif
}

void Dkc1DixieSwitchAndRelaunch(int enabled, const char *variant_exe,
                                const char *stock_exe, char *note,
                                size_t note_size) {
#if defined(_WIN32)
  if (!s_rom_path[0]) {
    if (note && note_size)
      snprintf(note, note_size, "select a ROM before switching Dixie");
    return;
  }
  const int previous = Dkc1DixieSavedEnabled();
  Dkc1DixieSetEnabled(enabled);
  const char *target = enabled ? variant_exe : stock_exe;
  char tail[8192];
  QuoteTail(tail, sizeof tail, 0, NULL, s_rom_path);
  /* Explicit menu choices override an automation startup preference. */
  const char *old_env = getenv("DKC1_DIXIE");
  char *saved_env = old_env ? _strdup(old_env) : NULL;
  _putenv_s("DKC1_DIXIE", enabled ? "1" : "0");
  if (SpawnSibling(target, tail)) {
    exit(0); /* the sibling owns the session */
  }
  _putenv_s("DKC1_DIXIE", saved_env ? saved_env : "");
  free(saved_env);
  Dkc1DixieSetEnabled(previous); /* roll back */
  if (note && note_size)
    snprintf(note, note_size, "could not start %s", target);
#elif defined(__APPLE__)
  if (!s_rom_path[0]) {
    if (note && note_size)
      snprintf(note, note_size, "select a ROM before switching Dixie");
    return;
  }
  const int previous = Dkc1DixieSavedEnabled();
  Dkc1DixieSetEnabled(enabled);
  setenv("DKC1_DIXIE", enabled ? "1" : "0", 1);
  unsetenv("DKC1_USER_DIR");
  unsetenv("DKC1_SAVESTATE_INPUT");
  unsetenv("DKC1_PAUSE_AFTER_FRAME");
  const char *target = enabled ? variant_exe : stock_exe;
  if (SpawnMacSibling(target, s_rom_path))
    exit(0);
  Dkc1DixieSetEnabled(previous);
  if (note && note_size)
    snprintf(note, note_size, "could not start %s", target);
#else
  (void)enabled; (void)variant_exe; (void)stock_exe;
  if (note && note_size)
    snprintf(note, note_size,
             "Dixie Kong Country switch is not supported on this platform");
#endif
}
