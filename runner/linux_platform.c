/* Native Linux implementation of the SDL host's platform UI contract.
 * Mac-prefixed entry points retain ABI compatibility with the untouched
 * Mac/Windows UI (see windows_platform.c for precedent -- it keeps the same
 * naming for the same reason). Settings live under the XDG user config
 * directory (via SDL_GetPrefPath), never in the ROM or the repo.
 *
 * Status (first Linux pass):
 *  - Graphics/controls settings persistence: implemented (INI-style file).
 *  - ROM / Baby Kong ROM / MSU-1 pickers: implemented via zenity or kdialog
 *    if present on $PATH, else a stdin/stdout prompt fallback.
 *  - Native menu bar: not applicable on Linux, left as no-ops. Pause,
 *    quicksave/load, fullscreen and quit already work from the keyboard
 *    (see HandleKey in sdl_host.c) independent of any menu.
 *  - Native pause/settings panel (Dkc1MacShowPauseMenu): stubbed to resume
 *    immediately for now -- graphics/control field editing via a real UI
 *    is a documented follow-up, not yet implemented.
 *  - Metal-presenter-shaped entry points are repurposed as the real
 *    Vulkan/OpenGL presenter dispatcher (see linux_presenter.c).
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "linux_platform.h"
#include "linux_presenter.h"
#ifdef DKC1_LINUX_QT
#include "linux_qt_dialog.h"
#endif
#include "macos_file_picker.h"
#include "macos_pause_menu.h"
#include "macos_controls.h"
#include "macos_metal_presenter.h"
#include "verified_rom.h"
#include "dkc1_edge_policy.h"
#include "dkc1_msu1.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------- */
/* Config file: simple "[section]\nkey=value" INI, one file under the SDL
 * pref path. Mirrors the field table windows_platform.c uses so the same
 * settings round-trip the same way on every desktop platform. */

static char s_config_path[4096];

static const char *ConfigPath(void) {
  if (s_config_path[0]) return s_config_path;
  const char *override = getenv("DKC1_USER_DIR");
  char *pref = override ? SDL_strdup(override)
                         : SDL_GetPrefPath("Flat2VR", "DKC1Recomp");
  snprintf(s_config_path, sizeof s_config_path, "%s/linux.ini",
           pref ? pref : ".");
  SDL_free(pref);
  return s_config_path;
}

/* Loads the whole file into memory once per process; small (a few KB). */
static char *ReadWholeFile(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0) { fclose(f); return NULL; }
  char *buf = malloc((size_t)size + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[got] = '\0';
  return buf;
}

static int FindLine(const char *text, const char *section, const char *key,
                     const char **value_start, size_t *value_len) {
  char header[192];
  snprintf(header, sizeof header, "[%s]", section);
  const char *sec = strstr(text, header);
  if (!sec) return 0;
  sec += strlen(header);
  const char *sec_end = strstr(sec, "\n[");
  size_t key_len = strlen(key);
  for (const char *line = sec; line && (!sec_end || line < sec_end);) {
    while (*line == '\n' || *line == '\r') line++;
    if (!*line || (sec_end && line >= sec_end)) break;
    const char *eq = strchr(line, '=');
    const char *eol = strchr(line, '\n');
    if (!eol) eol = line + strlen(line);
    if (eq && eq < eol && (size_t)(eq - line) == key_len &&
        strncmp(line, key, key_len) == 0) {
      *value_start = eq + 1;
      *value_len = (size_t)(eol - (eq + 1));
      return 1;
    }
    line = eol;
  }
  return 0;
}

static int ReadInt(const char *section, const char *key, int fallback) {
  char *text = ReadWholeFile(ConfigPath());
  if (!text) return fallback;
  const char *value; size_t len;
  int result = fallback;
  if (FindLine(text, section, key, &value, &len) && len > 0 && len < 32) {
    char tmp[32]; memcpy(tmp, value, len); tmp[len] = '\0';
    result = atoi(tmp);
  }
  free(text);
  return result;
}

static char *ReadPath(const char *key) {
  char *text = ReadWholeFile(ConfigPath());
  if (!text) return NULL;
  const char *value; size_t len;
  char *result = NULL;
  if (FindLine(text, "Paths", key, &value, &len) && len > 0) {
    result = malloc(len + 1);
    if (result) { memcpy(result, value, len); result[len] = '\0'; }
  }
  free(text);
  return result;
}

/* Rewrites one key=value pair, preserving everything else. Simple and not
 * fast, but this is called on settings changes only, not per frame. */
static void WriteValue(const char *section, const char *key,
                        const char *value) {
  const char *path = ConfigPath();
  char *old_text = ReadWholeFile(path);
  /* Sized from the actual content being written, not a fixed slack --
   * `value` can be an arbitrary-length filesystem path (ROM/MSU-1
   * locations routinely run well past a couple hundred bytes), and a
   * fixed 4096-byte budget on top of the existing file could be
   * overflowed by strcat/strncat below for a long enough one. */
  size_t cap = (old_text ? strlen(old_text) : 0) +
               strlen(section) + strlen(key) + strlen(value) + 64;
  char *out = malloc(cap);
  if (!out) { free(old_text); return; }
  out[0] = '\0';
  int wrote = 0;
  if (old_text) {
    char header[192];
    snprintf(header, sizeof header, "[%s]", section);
    const char *sec = strstr(old_text, header);
    if (sec) {
      /* Copy everything up to end of this section's existing key (if
       * present) or its end, replacing/inserting the key along the way. */
      const char *cursor = old_text;
      const char *sec_end = strstr(sec + strlen(header), "\n[");
      const char *scan = sec + strlen(header);
      const char *key_line = NULL, *key_line_end = NULL;
      size_t key_len = strlen(key);
      for (const char *line = scan; line && (!sec_end || line < sec_end);) {
        while (*line == '\n' || *line == '\r') line++;
        if (!*line || (sec_end && line >= sec_end)) break;
        const char *eq = strchr(line, '=');
        const char *eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);
        if (eq && eq < eol && (size_t)(eq - line) == key_len &&
            strncmp(line, key, key_len) == 0) {
          key_line = line; key_line_end = eol; break;
        }
        line = eol;
      }
      if (key_line) {
        strncat(out, cursor, (size_t)(key_line - cursor));
        strcat(out, key); strcat(out, "="); strcat(out, value);
        cursor = key_line_end;
        strcat(out, cursor);
      } else {
        const char *insert_at = sec_end ? sec_end : (old_text + strlen(old_text));
        strncat(out, cursor, (size_t)(insert_at - cursor));
        strcat(out, key); strcat(out, "="); strcat(out, value); strcat(out, "\n");
        strcat(out, insert_at);
      }
      wrote = 1;
    } else {
      strcpy(out, old_text);
    }
  }
  if (!wrote) {
    /* section_block used to be a fixed 512-byte stack buffer sized the
     * same way -- same overflow risk for a long value, so build it in
     * the already-correctly-sized `out` buffer directly instead. */
    strcat(out, "[");
    strcat(out, section);
    strcat(out, "]\n");
    strcat(out, key);
    strcat(out, "=");
    strcat(out, value);
    strcat(out, "\n");
  }
  free(old_text);
  FILE *f = fopen(path, "wb");
  if (f) { fputs(out, f); fclose(f); }
  free(out);
}

static void WriteInt(const char *section, const char *key, int value) {
  char text[32]; snprintf(text, sizeof text, "%d", value);
  WriteValue(section, key, text);
}
static void WritePath(const char *key, const char *path) {
  WriteValue("Paths", key, path ? path : "");
}

/* ---------------------------------------------------------------------- */
/* Graphics + controls settings, mirroring windows_platform.c's field
 * table so the same values mean the same thing on every desktop build. */

typedef struct Field {
  const char *name; size_t offset;
} Field;
#define F(n) {#n, offsetof(Dkc1GraphicsSettings, n)}
static const Field kGraphicsFields[] = {
  F(display), F(upscaler), F(screen), F(reconstruct_mode), F(strength),
  F(softness), F(shading), F(crt.preset), F(crt.scanlines),
  F(crt.sharpness), F(crt.mask), F(crt.mask_strength), F(crt.glow),
  F(crt.halation), F(crt.curvature), F(window_scale), F(fullscreen),
  F(aspect), F(edge), F(audio_enabled), F(volume), F(state_slot),
  F(aquatic_fixes),
};
#undef F

void Dkc1MacSaveGraphics(const Dkc1GraphicsSettings *s) {
  for (size_t i = 0; i < sizeof kGraphicsFields / sizeof *kGraphicsFields; i++)
    WriteInt("GraphicsV1", kGraphicsFields[i].name,
              *(const int *)((const char *)s + kGraphicsFields[i].offset));
}

void Dkc1MacLoadGraphics(Dkc1GraphicsSettings *s) {
  Dkc1GraphicsDefault(s);
  for (size_t i = 0; i < sizeof kGraphicsFields / sizeof *kGraphicsFields; i++) {
    int *v = (int *)((char *)s + kGraphicsFields[i].offset);
    *v = ReadInt("GraphicsV1", kGraphicsFields[i].name, *v);
  }
  Dkc1GraphicsClamp(s);
}

static void ControlsDefaults(Dkc1Controls *c) {
  memset(c, 0, sizeof *c);
  c->source[0] = 3;
  const int keys[] = {82, 81, 80, 79, 22, 29, 4, 27, 20, 26, 40, 229};
  const int pads[] = {12, 13, 14, 15, 2, 1, 4, 3, 10, 11, 7, 5};
  memcpy(c->keys[0], keys, sizeof keys);
  for (int p = 0; p < 2; p++) {
    c->deadzone[p] = 25;
    memcpy(c->pads[p], pads, sizeof pads);
  }
  c->assist_keys[0] = SDL_SCANCODE_BACKSPACE;
  c->assist_keys[1] = SDL_SCANCODE_TAB;
  c->assist_pads[0] = 109;
  c->assist_pads[1] = 111;
}

static void ControlsIo(Dkc1Controls *c, int save) {
  const char *names[] = {"source", "deadzone", "keys", "pads",
                          "assist_keys", "assist_pads", "assist_enabled"};
  int *arrays[] = {c->source, c->deadzone, &c->keys[0][0], &c->pads[0][0],
                    c->assist_keys, c->assist_pads, &c->assist_enabled};
  const int counts[] = {2, 2, 24, 24, 4, 4, 1};
  for (int group = 0; group < 7; group++)
    for (int i = 0; i < counts[group]; i++) {
      char key[80]; snprintf(key, sizeof key, "%s%d", names[group], i);
      int *value = &arrays[group][i];
      if (save) {
        WriteInt("ControlsV1", key, *value);
      } else {
        int v = ReadInt("ControlsV1", key, *value);
        int valid = group == 0 ? (v >= 0 && v <= 3)
                  : group == 1 ? (v >= 0 && v <= 100)
                  : (group == 2 || group == 4) ? (v >= 0 && v < SDL_NUM_SCANCODES)
                  : group == 6 ? (v == 0 || v == 1)
                  : ((v >= 0 && v <= 15) || (v >= 100 && v <= 111));
        if (valid) *value = v;
      }
    }
}

void Dkc1MacSaveControls(const Dkc1Controls *c) {
  Dkc1Controls copy = *c; ControlsIo(&copy, 1);
}
void Dkc1MacLoadControls(Dkc1Controls *c) {
  ControlsDefaults(c); ControlsIo(c, 0);
}

/* ---------------------------------------------------------------------- */
/* File pickers: zenity (GNOME/most desktops) or kdialog (KDE) if present
 * on $PATH, else a terminal prompt so the game is still usable headless
 * or over SSH+X forwarding without either installed. */

static int HaveCommand(const char *name) {
  char cmd[256];
  snprintf(cmd, sizeof cmd, "command -v %s >/dev/null 2>&1", name);
  return system(cmd) == 0;
}

static char *RunPicker(const char *title, const char *zenity_filter,
                        int directory) {
  char cmd[1024];
  if (HaveCommand("zenity")) {
    if (directory)
      snprintf(cmd, sizeof cmd,
               "zenity --file-selection --directory --title=\"%s\" 2>/dev/null",
               title);
    else
      snprintf(cmd, sizeof cmd,
               "zenity --file-selection --title=\"%s\" %s 2>/dev/null",
               title, zenity_filter ? zenity_filter : "");
  } else if (HaveCommand("kdialog")) {
    snprintf(cmd, sizeof cmd, "kdialog --%s --title \"%s\" 2>/dev/null",
             directory ? "getexistingdirectory" : "getopenfilename", title);
  } else {
    fprintf(stderr,
            "[linux-platform] no zenity/kdialog found; enter a path for "
            "\"%s\" on stdin (or leave empty to cancel): ",
            title);
    fflush(stderr);
    char line[4096];
    if (!fgets(line, sizeof line, stdin)) return NULL;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 0) return NULL;
    return strdup(line); /* malloc-owned, matching the header contract --
                             see the SDL_strdup/free mismatch note below */
  }
  FILE *pipe = popen(cmd, "r");
  if (!pipe) return NULL;
  char line[4096] = {0};
  char *got = fgets(line, sizeof line, pipe);
  pclose(pipe);
  if (!got) return NULL;
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    line[--len] = '\0';
  if (len == 0) return NULL;
  /* Plain malloc/strdup, not SDL_strdup: the header contract
   * (macos_file_picker.h) says "malloc-owned", and callers free() these
   * results (see Dkc1MacChooseRom below) rather than SDL_free() -- SDL's
   * allocator isn't guaranteed to be the same as libc's malloc. */
  return strdup(line);
}

char *Dkc1MacChooseRom(void) {
  char error[192]; size_t size;
  char *saved = ReadPath("DKC1");
  if (saved) {
    uint8_t *rom = Dkc1ReadVerifiedRom(saved, &size, error, sizeof error);
    if (rom) { free(rom); return saved; }
    free(saved); WritePath("DKC1", NULL);
  }
  for (;;) {
    char *path = RunPicker("Choose your DKC1 USA v1.0 ROM",
                             "--file-filter='SNES ROM | *.sfc *.smc'", 0);
    if (!path) return NULL;
    uint8_t *rom = Dkc1ReadVerifiedRom(path, &size, error, sizeof error);
    if (rom) { free(rom); WritePath("DKC1", path); return path; }
    free(path);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unsupported ROM", error,
                              NULL);
  }
}

char *Dkc1MacChooseBabyKongRom(void) {
  return RunPicker("Choose your DKC3 USA ROM",
                    "--file-filter='SNES ROM | *.sfc *.smc'", 0);
}
char *Dkc1MacSavedBabyKongRom(void) { return ReadPath("DKC3"); }
void Dkc1MacSetBabyKongRom(const char *p) { WritePath("DKC3", p); }
int Dkc1MacSavedBabyKongEnabled(void) { return ReadInt("Mods", "BabyKong", 0); }
void Dkc1MacSetBabyKongEnabled(int enabled) {
  WriteInt("Mods", "BabyKong", enabled != 0);
}
char *Dkc1MacSavedMsu1(void) { return ReadPath("MSU1"); }
void Dkc1MacClearMsu1(void) { WritePath("MSU1", NULL); }
char *Dkc1MacChooseMsu1(void) {
  /* No archive-extraction helper for Linux yet -- point directly at an
   * already-extracted folder containing track-N.pcm files. */
  char *dir = RunPicker("Choose extracted MSU-1 folder", NULL, 1);
  if (!dir) return NULL;
  char error[192];
  Dkc1Msu1 *probe = Dkc1Msu1Open(dir, error, sizeof error);
  if (!probe) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Not an MSU-1 folder",
                              error, NULL);
    free(dir);
    return NULL;
  }
  Dkc1Msu1Close(probe);
  WritePath("MSU1", dir);
  return dir;
}

Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void) {
  return (Dkc1MacFullscreenScaling)ReadInt("Host", "Scaling", 1);
}
void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling v) {
  WriteInt("Host", "Scaling", v);
}
Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void) {
  return (Dkc1EdgePolicy)ReadInt("GraphicsV1", "edge", kDkc1EdgeGlide);
}
void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy v) {
  WriteInt("GraphicsV1", "edge", v);
}

/* ---------------------------------------------------------------------- */
/* No native menu bar on Linux. Pause, quicksave/load, fullscreen, quit,
 * and the debug layer/provenance toggles all already work from the
 * keyboard (see HandleKey in sdl_host.c) independent of any menu, so this
 * is a real (if reduced) experience, not a dead end. */

void Dkc1MacInstallMenu(void) {}
void Dkc1MacUpdateGraphicsMenuState(int display, int upscaler, int screen) {
  (void)display; (void)upscaler; (void)screen;
}
void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1MacFullscreenScaling scaling,
                            Dkc1VideoAspect aspect, Dkc1EdgePolicy edge,
                            unsigned char layer_mask, int provenance,
                            int replacement_music, int baby_kong_enabled,
                            int baby_kong_ready, int dixie_enabled) {
  (void)paused; (void)fullscreen; (void)scaling; (void)aspect; (void)edge;
  (void)layer_mask; (void)provenance; (void)replacement_music;
  (void)baby_kong_enabled; (void)baby_kong_ready; (void)dixie_enabled;
}
/* Note: Dkc1MacMenuCommand, Dkc1MacHostStatus, Dkc1MacAssistEnabled,
 * Dkc1MacPauseMenuController and Dkc1MacApplyGraphics are implemented by
 * sdl_host.c itself (the host), not the platform layer -- Windows only
 * calls them too, matching windows_platform.c's split. Defining them here
 * as well produced duplicate-symbol link errors; don't re-add them. */

/* Pause/settings panel: a real Qt6 dialog (linux_qt_dialog.cpp) when built
 * with DKC1_LINUX_QT, otherwise a resume-only stub so the build still
 * works without Qt as a dependency. Esc still works as a soft-pause
 * toggle either way. */
static int s_pause_menu_open;
int Dkc1MacPauseMenuIsOpen(void) { return s_pause_menu_open; }
#ifdef DKC1_LINUX_QT
int Dkc1MacShowPauseMenu(void *window, Dkc1GraphicsSettings *settings,
                         Dkc1Controls *controls, int graphics_page) {
  s_pause_menu_open = 1;
  int resume = Dkc1LinuxQtShowDialog(window, settings, controls, graphics_page);
  s_pause_menu_open = 0;
  return resume;
}
#else
int Dkc1MacShowPauseMenu(void *window, Dkc1GraphicsSettings *settings,
                         Dkc1Controls *controls, int graphics_page) {
  (void)window; (void)settings; (void)controls; (void)graphics_page;
  static int warned;
  if (!warned) {
    fprintf(stderr,
            "[linux-platform] built without Qt (DKC1_LINUX_QT off or Qt6 "
            "not found); resuming. Graphics/controls can be edited by "
            "hand in %s\n",
            ConfigPath());
    warned = 1;
  }
  return 1; /* resume */
}
#endif
/* Windows opens the same pause/settings surface at the controls page for
 * its dedicated "Controls and Assist..." menu entry; Linux has no native
 * menu to call this from yet, but sdl_host.c still links against it. */
int Dkc1MacEditControls(Dkc1Controls *controls) {
  Dkc1GraphicsSettings graphics;
  Dkc1MacLoadGraphics(&graphics);
  return Dkc1MacShowPauseMenu(NULL, &graphics, controls, 4);
}

/* Mac display-link entry points are not selected on Linux; the presenter
 * below drives frame pacing off the swap call directly, same as Windows. */
int Dkc1MacDisplayLinkStart(void *w, double fps) {
  (void)w; (void)fps; return 0;
}
int Dkc1MacDisplayLinkWait(unsigned long long a, double b, double *c,
                           double *d, double *e, unsigned long long *f) {
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0;
}
void Dkc1MacDisplayLinkStop(void) {}

/* The "Metal presenter" entry points are the real Linux renderer: this is
 * where Vulkan-then-OpenGL selection happens (see linux_presenter.c). */
int Dkc1MacMetalPresenterStart(void *native_window, double preferred_hz,
                               Dkc1MacFullscreenScaling scaling,
                               int fullscreen) {
  return Dkc1LinuxPresenterStart((SDL_Window *)native_window, preferred_hz,
                                  scaling, fullscreen);
}
void Dkc1MacMetalPresenterQueueFrame(
    const uint32_t *pixels, int width, int height, int presentation_width,
    const Dkc1MacPresentationFrameInfo *info) {
  (void)info;
  Dkc1LinuxPresenterQueueFrame(pixels, width, height, presentation_width);
}
void Dkc1MacMetalPresenterSetGeometry(int presentation_width, int fullscreen) {
  Dkc1LinuxPresenterSetGeometry(presentation_width, fullscreen);
}
void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling scaling) {
  Dkc1LinuxPresenterSetScaling(scaling);
}
void Dkc1MacMetalPresenterSetActive(int active) {
  Dkc1LinuxPresenterSetActive(active);
}
void Dkc1MacMetalPresenterFlush(void) { Dkc1LinuxPresenterFlush(); }
void Dkc1MacMetalPresenterStop(void) { Dkc1LinuxPresenterStop(); }
void Dkc1MacMetalPresenterSetGraphics(const Dkc1GraphicsSettings *settings) {
  Dkc1LinuxPresenterSetGraphics(settings);
}

int Dkc1LinuxPlatformTest(const char *directory) {
  char path[4096];
  snprintf(path, sizeof path, "%s/linux_platform_test.ini", directory);
  setenv("DKC1_USER_DIR", directory, 1);
  s_config_path[0] = '\0';
  Dkc1GraphicsSettings gfx; Dkc1MacLoadGraphics(&gfx);
  gfx.window_scale = 4; gfx.volume = 42;
  Dkc1MacSaveGraphics(&gfx);
  Dkc1GraphicsSettings reloaded; Dkc1MacLoadGraphics(&reloaded);
  if (reloaded.window_scale != 4 || reloaded.volume != 42) {
    fprintf(stderr, "linux_platform: graphics settings did not round-trip\n");
    return 1;
  }
  Dkc1Controls controls; Dkc1MacLoadControls(&controls);
  controls.deadzone[0] = 77;
  Dkc1MacSaveControls(&controls);
  Dkc1Controls reloaded_controls; Dkc1MacLoadControls(&reloaded_controls);
  if (reloaded_controls.deadzone[0] != 77) {
    fprintf(stderr, "linux_platform: controls did not round-trip\n");
    return 1;
  }
  (void)path;
  puts("LINUX_PLATFORM_PASS: graphics + controls settings round-trip");
  return 0;
}
