/* Minimal interactive Win32 host for the DKC1 recompilation.
 *
 * One file, no SDL: GDI StretchDIBits presentation, waveOut audio, and
 * keyboard input mapped to the shared runtime's snes9x-style joypad bits
 * (bit0=B, 1=Y, 2=Select, 3=Start, 4=Up, 5=Down, 6=Left, 7=Right,
 *  8=A, 9=X, 10=L, 11=R).
 *
 * Keys: arrows = D-pad, Z=B (jump), X=Y (run/grab), A=X, S=A,
 *       Q=L, W=R, Enter=Start, Right Shift=Select, Esc=quit.
 */
#include "dkc1_blank_scan.h"
#include "dkc1_game.h"
#include "dkc1_invariant_monitor.h"
#include "dkc1_debug_dump.h"
#include "dkc1_flight_recorder.h"
#include "dkc1_script.h"
#include "dkc1_video.h"
#include "input_playback.h"
#include "verified_rom.h"
#include "wram_dump.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "audio_trace.h"
#include "sha256.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

enum {
  kScale = 2,
  kPanelWidth = 380,
  kAudioBuffers = 8,
  kAudioFramesPerBuffer = 536,
  kDefaultAudioPrerollBuffers = 1,
  /* Keep this aligned with common_rtl.c's RTL_AUDIO_TARGET_NATIVES.  The
   * occupancy servo is designed around four native 534-sample blocks. */
  kAudioRingStartFrames = 2136,
};

enum {
  kMenuQuickSave = 100,
  kMenuQuickLoad,
  kMenuSaveStateAs,
  kMenuLoadStateFrom,
  kMenuExportRepro,
  kMenuExit,
  kMenuPauseResume,
  kMenuSingleStep,
  kMenuFullscreen,
  kMenuTogglePanel,
  kMenuProvenance,
  kMenuFpsCounter,
  kMenuAspectNative,  /* kMenuAspect* stay contiguous for the radio group */
  kMenuAspectWidescreen,
  kMenuLayerComposite,  /* kMenuLayer* stay contiguous for the radio group */
  kMenuLayerBg1,
  kMenuLayerBg2,
  kMenuLayerBg3,
  kMenuLayerObj,
};

static const DWORD kWindowedStyle =
    WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;

/* Build identity, injected by the build scripts. A binary built outside
 * them still runs but self-identifies as untracked. */
#ifndef DKC1_BUILD_COMMIT
#define DKC1_BUILD_COMMIT "untracked"
#endif
#ifndef DKC1_BUILD_TIME
#define DKC1_BUILD_TIME "unknown-time"
#endif
#ifndef DKC1_BUILD_CONFIG
#define DKC1_BUILD_CONFIG "dev"
#endif

/* Dark theme palette. The debug panel already uses 18,21,25; the menu bar
 * sits slightly lighter so the strips read as distinct surfaces. */
#define DKC1_DARK_CLIENT RGB(18, 21, 25)
#define DKC1_DARK_MENUBAR RGB(24, 26, 30)
#define DKC1_DARK_MENUBAR_HOT RGB(58, 63, 72)
#define DKC1_DARK_TEXT RGB(222, 230, 238)
#define DKC1_DARK_TEXT_DIM RGB(128, 134, 142)

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static uint8_t s_aspect_wide_pixels[
    kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static long s_aspect_wide_frame = -1;
static BITMAPINFO s_bmi;
static HWND s_window;
static int s_running = 1;
static int s_width;
static int s_height;
static int s_panel_enabled = 1;
static int s_paused;
static int s_step_once;
static int s_script_loaded;
static int s_script_failed;
static int s_route_finished;
static int s_export_requested;
static int s_quicksave_requested;
static int s_quickload_requested;
static DWORD s_route_autoclose_ms;
static long s_route_frame_limit;
static ULONGLONG s_route_terminal_tick;
static int s_route_result_written;
static long s_host_frame;
static uint32_t s_last_input;
static uint32_t s_manual_input;
static HMENU s_menu;
static int s_fullscreen;
static WINDOWPLACEMENT s_windowed_placement;
static char s_pending_state_path[1024];
static int s_pending_state_op;  /* 0 none, 1 save-as, 2 load-from */
static int s_show_fps;
static double s_fps_value;
static int s_inspect_x = -1;
static int s_inspect_y = -1;
static int s_inspect_pending;
static char s_pixel_report[640] = "(click any pixel)";
static int s_auto_export_fired;
static char s_tail_bundle[1024];
static long s_tail_export_frame;
static long s_tail_deadline;
static char s_host_status[512] = "manual play";
static Dkc1InputPlayback s_input_playback;
static Dkc1WramDump s_wram_dump;

static uint16_t ReadWram16(unsigned address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

/* ---- build identity --------------------------------------------------- */

static char s_exe_hash[12] = "nohash";
static char s_build_id[160];

static void InitBuildIdentity(void) {
  char exe_path[MAX_PATH];
  if (GetModuleFileNameA(NULL, exe_path, sizeof exe_path)) {
    FILE *file = fopen(exe_path, "rb");
    if (file) {
      if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size > 0 && fseek(file, 0, SEEK_SET) == 0) {
          uint8_t *data = (uint8_t *)malloc((size_t)size);
          if (data && fread(data, 1, (size_t)size, file) == (size_t)size) {
            uint8_t digest[32];
            sha256_compute(data, (size_t)size, digest);
            snprintf(s_exe_hash, sizeof s_exe_hash, "%02x%02x%02x%02x",
                     digest[0], digest[1], digest[2], digest[3]);
          }
          free(data);
        }
      }
      fclose(file);
    }
  }
  snprintf(s_build_id, sizeof s_build_id, "%s %s %s exe:%s",
           DKC1_BUILD_COMMIT, DKC1_BUILD_CONFIG, DKC1_BUILD_TIME, s_exe_hash);
}

/* Sidecar recording which build produced a state, so a stale-executable
 * session can never silently mix states across incompatible builds. */
static void WriteStateBuildInfo(const char *state_path) {
  char side[1200];
  snprintf(side, sizeof side, "%s.buildinfo.json", state_path);
  FILE *file = fopen(side, "wb");
  if (!file)
    return;
  fprintf(file,
          "{\"schema\":\"dkc1.state-buildinfo.v1\",\"commit\":\"%s\","
          "\"config\":\"%s\",\"build_time\":\"%s\",\"exe_sha8\":\"%s\","
          "\"host_frame\":%ld,\"snes_frame\":%d,\"widescreen\":%s}\n",
          DKC1_BUILD_COMMIT, DKC1_BUILD_CONFIG, DKC1_BUILD_TIME, s_exe_hash,
          s_host_frame, snes_frame_counter,
          Dkc1VideoIsWidescreen() ? "true" : "false");
  fclose(file);
}

/* Reads the recorded producing commit from a state's sidecar. Returns 1
 * when a commit was recovered, 0 when the state has no sidecar (legacy or
 * externally produced). */
static int StateBuildCommit(const char *state_path, char *out,
                            size_t out_size) {
  char side[1200];
  snprintf(side, sizeof side, "%s.buildinfo.json", state_path);
  FILE *file = fopen(side, "rb");
  if (!file)
    return 0;
  char text[768] = {0};
  size_t got = fread(text, 1, sizeof text - 1, file);
  fclose(file);
  text[got] = 0;
  const char *key = strstr(text, "\"commit\":\"");
  if (!key)
    return 0;
  key += 10;
  size_t i = 0;
  for (; i + 1 < out_size && key[i] && key[i] != '"'; i++)
    out[i] = key[i];
  out[i] = 0;
  return 1;
}

/* Returns 1 to proceed with the load. States without a sidecar load
 * silently; a recorded commit that differs from this build requires an
 * explicit user decision. */
static int ConfirmStateBuildCompat(const char *state_path) {
  char commit[80];
  if (!StateBuildCommit(state_path, commit, sizeof commit))
    return 1;
  if (strcmp(commit, DKC1_BUILD_COMMIT) == 0)
    return 1;
  char message[512];
  snprintf(message, sizeof message,
           "This state was saved by a different build.\n\n"
           "State build:  %s\nThis build:   %s (%s)\n\n"
           "Loading across builds can produce divergence that is not a real "
           "bug. Load anyway?",
           commit, DKC1_BUILD_COMMIT, DKC1_BUILD_TIME);
  return MessageBoxA(s_window, message, "DKC1Recomp - build mismatch",
                     MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
}

static char s_rom_path[1024];

/* Detector-triggered evidence capture: when any always-on integrity
 * detector fires (scene-local cache violations, stream-retrodiction
 * mismatches, rendered-blank margins), auto-request the same repro bundle
 * F9 would export — the moment of first detection is preserved without
 * the player having to react. Opt-in: DKC1_AUTO_EXPORT=1 and the flight
 * recorder armed. One export per burst (10s cooldown). */
static void MaybeAutoExport(void) {
  static int s_mode = -1;
  static long s_seen_total;
  static long s_cooldown_until;
  if (s_mode < 0)
    s_mode = EnvironmentEnabled("DKC1_AUTO_EXPORT") ? 1 : 0;
  if (!s_mode || !Dkc1FlightRecorderEnabled())
    return;
  if ((s_host_frame & 15) != 0)
    return; /* poll every 16 frames; counters are cumulative */
  long total = Dkc1BlankScanEventCount() +
               Dkc1InvariantMonitorTotal();
  for (int layer = 0; layer < 2; layer++) {
    WsShadowMarginStat stat;
    WsShadowGetMarginStats(layer, &stat);
    total += (long)(stat.outOfRangeRead + stat.outOfRangeWrite +
                    stat.retrodictMismatch);
  }
  /* Shadow counters are cumulative, but transitions deliberately tear down
   * and rebuild scene-local presentation state.  Consume any diagnostics
   * accumulated while extended terrain is unavailable so a later poll (or
   * the first gameplay frame after a fade) cannot export an all-black
   * transition and mislabel it as a widescreen cull.  Rendered blank events
   * already apply this same terrain-ready policy in dkc1_blank_scan.c. */
  if (!Dkc1VideoTerrainReady()) {
    if (total > s_seen_total)
      s_seen_total = total;
    return;
  }
  if (total > s_seen_total) {
    s_seen_total = total;
    if (s_host_frame >= s_cooldown_until) {
      s_cooldown_until = s_host_frame + 600;
      s_export_requested = 1;
      s_auto_export_fired = 1;
      snprintf(s_host_status, sizeof s_host_status,
               "integrity detector fired (total %ld) — auto-exporting",
               total);
    }
  }
}

/* Fire-and-forget same-frame layer isolation over a freshly exported repro
 * bundle's current.snapshot. Out of process so this session's PPU/HDMA
 * state is untouched; the tool reloads the snapshot per layer mask, which
 * guarantees every image shows the same emulated frame. */
static void SpawnLayerCapture(const char *bundle_dir) {
  char exe[MAX_PATH];
  if (!GetModuleFileNameA(NULL, exe, sizeof exe))
    return;
  char *slash = strrchr(exe, '\\');
  if (!slash)
    return;
  snprintf(slash + 1, sizeof exe - (size_t)(slash + 1 - exe),
           "dkc1_layer_capture.exe");
  if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
    return;
  char command[2048];
  snprintf(command, sizeof command,
           "\"%s\" \"%s\" \"%s\\current.snapshot\" \"%s\"", exe, s_rom_path,
           bundle_dir, bundle_dir);
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  memset(&startup, 0, sizeof startup);
  startup.cb = sizeof startup;
  if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                     NULL, NULL, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
}

static int EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

/* ---- dark theme ------------------------------------------------------ */

static HBRUSH MenubarBrush(void) {
  static HBRUSH brush;
  if (!brush) brush = CreateSolidBrush(DKC1_DARK_MENUBAR);
  return brush;
}

static HBRUSH MenubarHotBrush(void) {
  static HBRUSH brush;
  if (!brush) brush = CreateSolidBrush(DKC1_DARK_MENUBAR_HOT);
  return brush;
}

static HFONT MenuFont(void) {
  static HFONT font;
  if (!font) {
    NONCLIENTMETRICSA metrics;
    memset(&metrics, 0, sizeof metrics);
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof metrics,
                              &metrics, 0))
      font = CreateFontIndirectA(&metrics.lfMenuFont);
    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  }
  return font;
}

/* Documented on Windows 10 1809+ (attribute 19) / 20H1+ (attribute 20). */
static void EnableDarkTitleBar(HWND hwnd) {
  HMODULE dwm = LoadLibraryA("dwmapi.dll");
  if (!dwm) return;
  typedef HRESULT(WINAPI * SetAttrFn)(HWND, DWORD, LPCVOID, DWORD);
  SetAttrFn set_attr = (SetAttrFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
  if (set_attr) {
    BOOL dark = TRUE;
    if (FAILED(set_attr(hwnd, 20, &dark, sizeof dark)))
      set_attr(hwnd, 19, &dark, sizeof dark);
  }
}

/* Dark popup menus: uxtheme ordinal 135 = SetPreferredAppMode(2 =
 * ForceDark), 136 = FlushMenuThemes. Undocumented but stable since
 * Windows 10 1903 and used by mainstream apps; degrades to light menus
 * if either export is missing. */
static void EnableDarkMenus(void) {
  HMODULE uxtheme = LoadLibraryA("uxtheme.dll");
  if (!uxtheme) return;
  typedef int(WINAPI * SetModeFn)(int);
  typedef void(WINAPI * FlushFn)(void);
  SetModeFn set_mode =
      (SetModeFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
  FlushFn flush = (FlushFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
  if (set_mode) set_mode(2);
  if (flush) flush();
}

/* The classic menu BAR ignores dark app mode entirely; the shell instead
 * sends these undocumented-but-stable UAH messages that let the window
 * paint the bar itself (the standard Win32 dark-menubar technique). */
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092

typedef struct {
  HMENU hmenu;
  HDC hdc;
  DWORD dwFlags;
} UahMenu;

typedef struct {
  DWORD rgSize[8];  /* item metrics union; layout not needed for drawing */
  DWORD rgcx[4];
  DWORD fUpdateMaxWidths : 2;
} UahMenuItemMetrics;

typedef struct {
  int iPosition;
  UahMenuItemMetrics umim;
} UahMenuItem;

typedef struct {
  DRAWITEMSTRUCT dis;
  UahMenu um;
  UahMenuItem umi;
} UahDrawMenuItem;

static int DrawDarkMenuBarBackground(HWND hwnd, const UahMenu *menu) {
  MENUBARINFO bar;
  memset(&bar, 0, sizeof bar);
  bar.cbSize = sizeof bar;
  if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &bar)) return 0;
  RECT window_rect;
  GetWindowRect(hwnd, &window_rect);
  RECT rect = bar.rcBar;
  OffsetRect(&rect, -window_rect.left, -window_rect.top);
  FillRect(menu->hdc, &rect, MenubarBrush());
  return 1;
}

static int DrawDarkMenuBarItem(const UahDrawMenuItem *item) {
  char text[64] = "";
  MENUITEMINFOA info;
  memset(&info, 0, sizeof info);
  info.cbSize = sizeof info;
  info.fMask = MIIM_STRING;
  info.dwTypeData = text;
  info.cch = sizeof text - 1;
  if (!GetMenuItemInfoA(item->um.hmenu, (UINT)item->umi.iPosition, TRUE,
                        &info))
    return 0;
  const UINT state = item->dis.itemState;
  HDC dc = item->um.hdc;
  RECT rect = item->dis.rcItem;
  FillRect(dc, &rect,
           (state & (ODS_HOTLIGHT | ODS_SELECTED)) ? MenubarHotBrush()
                                                   : MenubarBrush());
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, (state & (ODS_GRAYED | ODS_DISABLED | ODS_INACTIVE))
                       ? DKC1_DARK_TEXT_DIM
                       : DKC1_DARK_TEXT);
  HFONT old_font = (HFONT)SelectObject(dc, MenuFont());
  /* '&' marks the Alt accelerator; DT_HIDEPREFIX consumes it like the
   * native menu bar does instead of printing it literally. */
  DrawTextA(dc, text, -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_HIDEPREFIX);
  SelectObject(dc, old_font);
  return 1;
}

/* DefWindowProc redraws a light 1px separator under the bar during
 * non-client painting; paint it back over in bar color. */
static void PaintOverMenuBarLine(HWND hwnd) {
  MENUBARINFO bar;
  memset(&bar, 0, sizeof bar);
  bar.cbSize = sizeof bar;
  if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &bar)) return;
  RECT window_rect;
  GetWindowRect(hwnd, &window_rect);
  RECT line = bar.rcBar;
  OffsetRect(&line, -window_rect.left, -window_rect.top);
  line.top = line.bottom;
  line.bottom = line.top + 1;
  HDC dc = GetWindowDC(hwnd);
  if (!dc) return;
  FillRect(dc, &line, MenubarBrush());
  ReleaseDC(hwnd, dc);
}

static void UpdateDebugTitle(void);

static uint32_t InputBitForVirtualKey(WPARAM key) {
  switch (key) {
    case 'Z': return 0x001;       /* B */
    case 'X': return 0x002;       /* Y */
    case VK_RSHIFT: return 0x004; /* Select */
    case VK_RETURN: return 0x008; /* Start */
    case VK_UP: return 0x010;
    case VK_DOWN: return 0x020;
    case VK_LEFT: return 0x040;
    case VK_RIGHT: return 0x080;
    case 'S': return 0x100;       /* A */
    case 'A': return 0x200;       /* X */
    case 'Q': return 0x400;       /* L */
    case 'W': return 0x800;       /* R */
    default: return 0;
  }
}

static void WriteRouteResult(const char *status) {
  if (s_route_result_written) return;
  s_route_result_written = 1;
  const char *path = getenv("DKC1_ROUTE_RESULT");
  if (!path || !*path) return;
  char temporary[1200];
  snprintf(temporary, sizeof temporary, "%s.tmp-%lu", path,
           (unsigned long)GetCurrentProcessId());
  FILE *file = fopen(temporary, "wb");
  if (!file) return;
  fprintf(file,
          "{\"schema\":\"dkc1.visible-route-result.v1\","
          "\"status\":\"%s\",\"host_frame\":%ld,"
          "\"snes_frame\":%d,\"widescreen\":%s}\n",
          status, s_host_frame, snes_frame_counter,
          Dkc1VideoIsWidescreen() ? "true" : "false");
  fflush(file);
  fclose(file);
  if (!MoveFileExA(temporary, path,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    DeleteFileA(temporary);
}

static void SetRouteTerminal(int failed, const char *status,
                             const char *message) {
  s_route_finished = !failed;
  s_script_failed = failed;
  s_paused = 1;
  s_step_once = 0;
  snprintf(s_host_status, sizeof s_host_status, "%s", message);
  WriteRouteResult(status);
  s_route_terminal_tick = GetTickCount64();
  UpdateDebugTitle();
}

static const char *LayerModeName(uint8_t mask) {
  switch (mask) {
    case 0x01: return "BG1";
    case 0x02: return "BG2";
    case 0x04: return "BG3";
    case 0x08: return "BG4";
    case 0x10: return "OBJ";
    default: return "composite";
  }
}

static HMENU BuildMenuBar(void) {
  HMENU file = CreatePopupMenu();
  AppendMenuA(file, MF_STRING, kMenuQuickSave, "Quick &Save State\tF11");
  AppendMenuA(file, MF_STRING, kMenuQuickLoad, "Quick &Load State\tF12");
  AppendMenuA(file, MF_STRING, kMenuSaveStateAs, "Save State &As...");
  AppendMenuA(file, MF_STRING, kMenuLoadStateFrom, "Load State &From...");
  AppendMenuA(file, MF_SEPARATOR, 0, NULL);
  AppendMenuA(file, MF_STRING, kMenuExportRepro, "Export Repro &Bundle\tF9");
  AppendMenuA(file, MF_SEPARATOR, 0, NULL);
  AppendMenuA(file, MF_STRING, kMenuExit, "E&xit\tEsc");
  HMENU emulation = CreatePopupMenu();
  AppendMenuA(emulation, MF_STRING, kMenuPauseResume, "&Pause/Resume\tF7");
  AppendMenuA(emulation, MF_STRING, kMenuSingleStep, "Single &Step\tF8");
  HMENU layers = CreatePopupMenu();
  AppendMenuA(layers, MF_STRING, kMenuLayerComposite, "&Composite\tF2");
  AppendMenuA(layers, MF_STRING, kMenuLayerBg1, "BG&1 only\tF3");
  AppendMenuA(layers, MF_STRING, kMenuLayerBg2, "BG&2 only\tF4");
  AppendMenuA(layers, MF_STRING, kMenuLayerBg3, "BG&3 only\tF5");
  AppendMenuA(layers, MF_STRING, kMenuLayerObj, "&Sprites only\tF6");
  HMENU aspect = CreatePopupMenu();
  AppendMenuA(aspect, MF_STRING, kMenuAspectNative, "Native &4:3 (256x224)");
  AppendMenuA(aspect, MF_STRING, kMenuAspectWidescreen,
              "Widescreen &16:9 (342x224)");
  HMENU view = CreatePopupMenu();
  AppendMenuA(view, MF_STRING, kMenuFullscreen, "&Fullscreen\tAlt+Enter");
  AppendMenuA(view, MF_STRING, kMenuTogglePanel, "Debug &Panel");
  AppendMenuA(view, MF_STRING, kMenuProvenance, "Pro&venance Overlay\tF1");
  AppendMenuA(view, MF_STRING, kMenuFpsCounter, "FPS &Counter");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)aspect, "&Aspect Ratio");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)layers, "&Layers");
  HMENU bar = CreateMenu();
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)emulation, "&Emulation");
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
  return bar;
}

static void RefreshMenuChecks(void) {
  if (!s_menu) return;
  CheckMenuItem(s_menu, kMenuPauseResume,
                MF_BYCOMMAND | (s_paused ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(s_menu, kMenuFullscreen,
                MF_BYCOMMAND | (s_fullscreen ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(s_menu, kMenuTogglePanel,
                MF_BYCOMMAND | (s_panel_enabled ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuItem(s_menu, kMenuProvenance,
                MF_BYCOMMAND |
                    (Dkc1DebugProvenanceOverlay() ? MF_CHECKED
                                                  : MF_UNCHECKED));
  CheckMenuItem(s_menu, kMenuFpsCounter,
                MF_BYCOMMAND | (s_show_fps ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuRadioItem(s_menu, kMenuAspectNative, kMenuAspectWidescreen,
                     Dkc1VideoIsWidescreen() ? kMenuAspectWidescreen
                                             : kMenuAspectNative,
                     MF_BYCOMMAND);
  UINT layer_item;
  switch (Dkc1DebugLayerMask()) {
    case 0x01: layer_item = kMenuLayerBg1; break;
    case 0x02: layer_item = kMenuLayerBg2; break;
    case 0x04: layer_item = kMenuLayerBg3; break;
    case 0x10: layer_item = kMenuLayerObj; break;
    default: layer_item = kMenuLayerComposite; break;
  }
  CheckMenuRadioItem(s_menu, kMenuLayerComposite, kMenuLayerObj,
                     layer_item, MF_BYCOMMAND);
}

static void UpdateDebugTitle(void) {
  if (!s_window) return;
  char title[320];
  snprintf(title, sizeof title,
           "DKC1Recomp %s | frame %ld | %s | %s | %s | provenance %s",
           DKC1_BUILD_COMMIT, s_host_frame, s_paused ? "PAUSED" : "running",
           Dkc1VideoIsWidescreen() ? "16:9" : "4:3",
           LayerModeName(Dkc1DebugLayerMask()),
           Dkc1DebugProvenanceOverlay() ? "ON" : "off");
  SetWindowTextA(s_window, title);
  RefreshMenuChecks();
}

/* Click-to-provenance: resolve a queued click one frame later, when the
 * provenance surface has been filled by a render pass with capture armed.
 * Everything here is read-only against emulated state. */
static void ResolvePixelInspect(void) {
  if (!s_inspect_pending || s_inspect_x < 0)
    return;
  if (--s_inspect_pending)
    return;
  static const char *const kProvNames[] = {
      "none", "captured", "prefill", "fold", "blank", "raw-cont",
      "raw-fallback", "OUT-OF-RANGE"};
  const int extra = Dkc1VideoIsWidescreen() ? Dkc1VideoExtra() : 0;
  const int native_x = s_inspect_x - extra;
  const uint16_t cam_x = ReadWram16(0x088b);
  const uint16_t cam_y = ReadWram16(0x0895);
  int offset = snprintf(s_pixel_report, sizeof s_pixel_report,
                        "px(%d,%d) native x=%d%s", s_inspect_x, s_inspect_y,
                        native_x, "\r\n");
  for (int layer = 0; layer < 2; layer++) {
    if (!WsShadowLayerActive(layer))
      continue;
    const unsigned shift = PPU_bigTiles(g_ppu, layer) ? 4u : 3u;
    const uint32_t world_x =
        WsShadowWorldX(layer) + (uint32_t)native_x;
    const uint32_t world_y =
        WsShadowPresentWorldY(layer, native_x) + (uint32_t)s_inspect_y;
    const uint32_t tile_x = world_x >> shift;
    const uint32_t tile_y = world_y >> shift;
    uint16_t entry = 0;
    const int cell = WsShadowDebugCell(layer, tile_x, tile_y, &entry);
    const uint8_t prov =
        WsShadowDebugProvenanceAt(layer, native_x, s_inspect_y);
    uint32_t writer_frame = 0;
    const int writer =
        WsShadowDebugLastWriter(layer, tile_x, tile_y, &writer_frame);
    offset += snprintf(
        s_pixel_report + offset,
        sizeof s_pixel_report - (size_t)offset,
        "L%d world(%u,%u) tile(%u,%u) e=%04X %s%s%s wr=%s@f%u%s",
        layer, world_x, world_y, tile_x, tile_y, entry,
        cell == 2 ? "guess " : cell == 1 ? "vram " : "empty ",
        prov < 8 ? kProvNames[prov] : "?",
        "", WsShadowWriteKindName(writer), writer_frame, "\r\n");
    if ((size_t)offset >= sizeof s_pixel_report - 96)
      break;
  }
  /* OAM entries near the pixel (WRAM shadow, native screen space). */
  int oam_hits = 0;
  for (int i = 0; i < 128 && oam_hits < 2; i++) {
    const uint8_t *entry8 = g_ram + 0x0200 + i * 4;
    const uint8_t hi =
        (uint8_t)((g_ram[0x0400 + i / 4] >> ((i % 4) * 2)) & 3);
    int sx = entry8[0] | ((hi & 1) << 8);
    if (sx >= 256)
      sx -= 512;
    const int sy = entry8[1];
    if (sy >= 0xF0)
      continue;
    if (native_x - sx >= -8 && native_x - sx < 40 &&
        s_inspect_y - sy >= -8 && s_inspect_y - sy < 40) {
      oam_hits++;
      offset += snprintf(s_pixel_report + offset,
                         sizeof s_pixel_report - (size_t)offset,
                         "OAM#%d x=%d y=%d t=%02X a=%02X%s", i, sx, sy,
                         entry8[2], entry8[3], "\r\n");
    }
  }
  /* Nearest allocated actor by screen distance. */
  int best_slot = -1, best_distance = 0x7fffffff;
  for (unsigned index = 0x02; index <= 0x32; index += 2) {
    if (!ReadWram16(0x0D45 + index))
      continue;
    int rel_x = (int)(uint16_t)(ReadWram16(0x0B19 + index) - cam_x);
    if (rel_x >= 0x8000) rel_x -= 0x10000;
    int rel_y = (int)(uint16_t)(ReadWram16(0x0BC1 + index) - cam_y);
    if (rel_y >= 0x8000) rel_y -= 0x10000;
    const int dx = rel_x - native_x, dy = rel_y - s_inspect_y;
    const int distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (distance < best_distance) {
      best_distance = distance;
      best_slot = (int)index;
    }
  }
  if (best_slot >= 0 && best_distance < 160) {
    offset += snprintf(
        s_pixel_report + offset, sizeof s_pixel_report - (size_t)offset,
        "actor idx%02X id=%u src=%d st=%04X d=%d", best_slot,
        ReadWram16(0x0D45 + (unsigned)best_slot),
        (int16_t)ReadWram16(0x15FD + (unsigned)best_slot),
        ReadWram16(0x1029 + (unsigned)best_slot), best_distance);
    /* Close the pixel->PC loop: hand the exact backward query for this
     * actor's state word to the reverse-watch tool. */
    offset += snprintf(
        s_pixel_report + offset, sizeof s_pixel_report - (size_t)offset,
        "%snext: reverse_watch --address %X:2 --before-frame %ld",
        "\r\n", 0x1029 + (unsigned)best_slot, s_host_frame);
  }
  (void)offset;
  snprintf(s_host_status, sizeof s_host_status,
           "pixel (%d,%d) inspected", s_inspect_x, s_inspect_y);
}

/* Host-side FPS badge, composed off-screen like the panel so nothing
 * flickers; never rendered into the framebuffer evidence. */
static void DrawFpsBadge(HDC dc, int x, int y) {
  enum { kBadgeWidth = 96, kBadgeHeight = 22 };
  static HDC badge_dc;
  static HBITMAP badge_bitmap;
  if (!badge_dc) {
    badge_dc = CreateCompatibleDC(dc);
    badge_bitmap = CreateCompatibleBitmap(dc, kBadgeWidth, kBadgeHeight);
    SelectObject(badge_dc, badge_bitmap);
  }
  RECT rect = {0, 0, kBadgeWidth, kBadgeHeight};
  FillRect(badge_dc, &rect, MenubarBrush());
  SetBkMode(badge_dc, TRANSPARENT);
  SetTextColor(badge_dc, RGB(126, 217, 87));
  HFONT old_font =
      (HFONT)SelectObject(badge_dc, GetStockObject(ANSI_FIXED_FONT));
  char text[32];
  snprintf(text, sizeof text, "%5.1f FPS", s_fps_value);
  DrawTextA(badge_dc, text, -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(badge_dc, old_font);
  BitBlt(dc, x, y, kBadgeWidth, kBadgeHeight, badge_dc, 0, 0, SRCCOPY);
}

static void PresentFrame(HDC dc) {
  if (!dc || !s_window || !s_width || !s_height) return;
  if (!s_bmi.bmiHeader.biSize) return;
  if (s_fullscreen) {
    /* Aspect-preserving letterbox across the whole monitor; the debug
     * panel stays windowed-only. Bars are repainted with the same black
     * every frame, so there is nothing to flicker. */
    RECT client;
    GetClientRect(s_window, &client);
    const int cw = client.right, ch = client.bottom;
    if (cw <= 0 || ch <= 0) return;
    int dw = cw, dh = cw * s_height / s_width;
    if (dh > ch) {
      dh = ch;
      dw = ch * s_width / s_height;
    }
    const int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bar;
    if (dy > 0) {
      SetRect(&bar, 0, 0, cw, dy);
      FillRect(dc, &bar, black);
      SetRect(&bar, 0, dy + dh, cw, ch);
      FillRect(dc, &bar, black);
    }
    if (dx > 0) {
      SetRect(&bar, 0, 0, dx, ch);
      FillRect(dc, &bar, black);
      SetRect(&bar, dx + dw, 0, cw, ch);
      FillRect(dc, &bar, black);
    }
    SetStretchBltMode(dc, COLORONCOLOR);  /* crisp pixels, no smoothing */
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, s_width, s_height,
                  s_pixels, &s_bmi, DIB_RGB_COLORS, SRCCOPY);
    if (s_show_fps) DrawFpsBadge(dc, dx + 8, dy + 8);
    return;
  }
  StretchDIBits(dc, 0, 0, s_width * kScale, s_height * kScale,
                0, 0, s_width, s_height, s_pixels, &s_bmi,
                DIB_RGB_COLORS, SRCCOPY);
  if (s_show_fps) DrawFpsBadge(dc, 8, 8);
  if (!s_panel_enabled) return;

  /* Compose the panel off-screen: FillRect-then-DrawText straight onto the
   * window DC lets the display sample between the background wipe and the
   * glyph pass, which reads as constant text flicker. A finished buffer
   * blitted once per frame is atomic. */
  const int panel_height = s_height * kScale;
  static HDC panel_dc;
  static HBITMAP panel_bitmap;
  static int panel_buffer_height;
  if (!panel_dc || panel_buffer_height != panel_height) {
    if (panel_bitmap) DeleteObject(panel_bitmap);
    if (panel_dc) DeleteDC(panel_dc);
    panel_dc = CreateCompatibleDC(dc);
    panel_bitmap = CreateCompatibleBitmap(dc, kPanelWidth, panel_height);
    SelectObject(panel_dc, panel_bitmap);
    panel_buffer_height = panel_height;
  }

  RECT panel = {0, 0, kPanelWidth, panel_height};
  HBRUSH background = CreateSolidBrush(RGB(18, 21, 25));
  FillRect(panel_dc, &panel, background);
  DeleteObject(background);
  SetBkMode(panel_dc, TRANSPARENT);
  SetTextColor(panel_dc, RGB(222, 230, 238));
  HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
  HFONT old_font = (HFONT)SelectObject(panel_dc, font);

  char invariant_summary[160];
  char script[256] = "manual keyboard input";
  if (s_script_loaded) Dkc1ScriptStatus(script, sizeof script);
  else if (s_input_playback.count)
    snprintf(script, sizeof script, "input playback: %zu frames",
             s_input_playback.count);
  char text[2048];
  snprintf(text, sizeof text,
           "VISIBLE WIDESCREEN DEBUGGER\r\n"
           "Build: %s\r\n"
           "\r\n"
           "Host frame: %ld\r\n"
           "State: %s%s%s\r\n"
           "Input: $%03X\r\n"
           "Route: %s\r\n"
           "Status: %s\r\n"
           "\r\n"
           "Mode / level / entrance\r\n"
           "$%04X / $%04X / $%04X\r\n"
           "Layer scroll X/Y: $%04X / $%04X\r\n"
           "Camera bounds: $%04X .. $%04X\r\n"
           "Scanner: rec $%02X  range $%04X..$%04X (%u px)\r\n"
           "Section: state $%04X  records $%04X..$%04X  limit $%04X\r\n"
           "Widescreen world: %s  extra %d px/side\r\n"
           "\r\n"
           "Evidence taps\r\n"
           "WS trace: %s\r\n"
           "OAM: %s   lifecycle: %s\r\n"
           "WRAM dump: %s   input record: %s\r\n"
           "Flight recorder: %s\r\n"
           "Invariants: %s\r\n"
           "Pixel inspect (click view):\r\n%s\r\n"
           "\r\n"
           "F1 provenance   F2 composite\r\n"
           "F3 BG1  F4 BG2  F5 BG3  F6 OBJ\r\n"
           "F7 pause/resume   F8 single-step\r\n"
           "F9 export rolling repro bundle\r\n"
           "F11 quick save   F12 quick load\r\n"
           "Alt+Enter fullscreen; Esc returns\r\n"
           "Esc quit (when windowed)\r\n"
           "\r\n"
           "The side panel is host-only and is not\r\n"
           "included in framebuffer evidence.",
           s_build_id,
           s_host_frame,
           s_paused ? "PAUSED" : "running",
           s_route_finished ? " / ROUTE COMPLETE" : "",
           s_script_failed ? " / FAILED" : "", s_last_input,
           script, s_host_status,
           ReadWram16(0x0032), ReadWram16(0x0030), ReadWram16(0x003e),
           ReadWram16(0x088b), ReadWram16(0x0895),
           ReadWram16(0x1b23), ReadWram16(0x1b25),
           (unsigned)g_ram[0x00a4], ReadWram16(0x00ef),
           ReadWram16(0x00f1),
           (unsigned)(uint16_t)(ReadWram16(0x00f1) - ReadWram16(0x00ef)),
           ReadWram16(0x1e03), ReadWram16(0x1e07), ReadWram16(0x1e09),
           ReadWram16(0x1e0b),
           Dkc1VideoTerrainReady() ? "READY" : "not ready",
           Dkc1VideoExtra(),
           EnvironmentEnabled("DKC1_WS_TRACE") ? "ON" : "off",
           EnvironmentEnabled("DKC1_OAM_LOG") ? "ON" : "off",
           EnvironmentEnabled("DKC1_LIFECYCLE_TRACE") ? "ON" : "off",
           EnvironmentEnabled("DKC1_WRAM_DUMP") ? "ON" : "off",
           EnvironmentEnabled("DKC1_INPUT_RECORD") ? "ON" : "off",
           Dkc1FlightRecorderEnabled() ? "ARMED (60 seconds)" : "off",
           Dkc1InvariantMonitorSummary(invariant_summary,
                                       sizeof invariant_summary),
           s_pixel_report);
  RECT text_rect = panel;
  text_rect.left += 12;
  text_rect.top += 12;
  text_rect.right -= 10;
  DrawTextA(panel_dc, text, -1, &text_rect,
            DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
  SelectObject(panel_dc, old_font);
  BitBlt(dc, s_width * kScale, 0, kPanelWidth, panel_height,
         panel_dc, 0, 0, SRCCOPY);
}

static HWAVEOUT s_waveout;
static WAVEHDR s_wave_headers[kAudioBuffers];
static int16_t s_wave_data[kAudioBuffers][kAudioFramesPerBuffer * 2];
static int s_wave_index;
static double s_audio_accumulator;
static double s_host_frame_rate = 60.098811862;
static int s_audio_started;
static int s_audio_preroll_buffers = kDefaultAudioPrerollBuffers;
static int s_audio_last_queued_frames;
static unsigned long s_audio_starvations;
static unsigned long s_audio_drops;
static int s_audio_waiting_for_ring = 1;
static unsigned long s_audio_ring_start_threshold = kAudioRingStartFrames;
static int s_audio_log_stats;
static unsigned long s_audio_ring_frames;
static unsigned long long s_audio_internal_underflows;

/* Resize the windowed frame to fit the game view plus the optional panel. */
static void ApplyWindowedSize(void) {
  if (!s_window || s_fullscreen) return;
  RECT rect = { 0, 0,
                s_width * kScale + (s_panel_enabled ? kPanelWidth : 0),
                s_height * kScale };
  AdjustWindowRect(&rect, kWindowedStyle, TRUE);
  SetWindowPos(s_window, NULL, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(s_window, NULL, FALSE);
}

/* Switch the presentation aspect without touching SNES state.  The renderer
 * owns separate native/wide presentation history, while the host must update
 * both its source pitch and DIB width before the next frame is drawn. */
static void SetAspectMode(int widescreen) {
  int requested = widescreen != 0;
  if (Dkc1VideoIsWidescreen() == requested) return;

  const int old_width = s_width;
  if (old_width == kDkc1VideoWidescreenWidth) {
    memcpy(s_aspect_wide_pixels, s_pixels, sizeof s_aspect_wide_pixels);
    s_aspect_wide_frame = s_host_frame;
  }
  Dkc1VideoSetWidescreen(requested);
  s_width = Dkc1VideoWidth();
  s_bmi.bmiHeader.biWidth = s_width;

  /* Keep a paused frame intelligible without running an extra emulation or
   * PPU frame. Wide -> native crops the authentic center. Native -> wide
   * centers that same frame over black until the next ordinary frame builds
   * fresh margins. A temporary buffer avoids overlapping row-stride moves. */
  if (s_width == kDkc1VideoWidescreenWidth &&
      s_aspect_wide_frame == s_host_frame) {
    /* A paused wide -> native -> wide comparison can restore the exact wide
     * frame because no cartridge frame has elapsed in between. */
    memcpy(s_pixels, s_aspect_wide_pixels, sizeof s_aspect_wide_pixels);
  } else {
    static uint8_t remapped[kDkc1VideoWidescreenWidth *
                            kDkc1VideoHeight * 4];
    const int copy_width = old_width < s_width ? old_width : s_width;
    const int source_x = old_width > s_width ? (old_width - s_width) / 2 : 0;
    const int dest_x = s_width > old_width ? (s_width - old_width) / 2 : 0;
    memset(remapped, 0, sizeof remapped);
    for (int y = 0; y < s_height; y++) {
      memcpy(remapped + ((size_t)y * s_width + dest_x) * 4,
             s_pixels + ((size_t)y * old_width + source_x) * 4,
             (size_t)copy_width * 4);
    }
    memcpy(s_pixels, remapped,
           (size_t)s_width * (size_t)s_height * 4);
  }
  Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);
  ApplyWindowedSize();
  snprintf(s_host_status, sizeof s_host_status,
           "aspect changed to %s (%dx%d)",
           requested ? "widescreen 16:9" : "native 4:3",
           s_width, s_height);
  UpdateDebugTitle();
  InvalidateRect(s_window, NULL, FALSE);
}

static void SetFullscreen(int enable) {
  if (!s_window || enable == s_fullscreen) return;
  s_fullscreen = enable;
  if (enable) {
    s_windowed_placement.length = sizeof s_windowed_placement;
    GetWindowPlacement(s_window, &s_windowed_placement);
    SetMenu(s_window, NULL);
    SetWindowLongA(s_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    HMONITOR monitor = MonitorFromWindow(s_window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    info.cbSize = sizeof info;
    GetMonitorInfoA(monitor, &info);
    SetWindowPos(s_window, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  } else {
    SetWindowLongA(s_window, GWL_STYLE, kWindowedStyle | WS_VISIBLE);
    SetMenu(s_window, s_menu);
    SetWindowPlacement(s_window, &s_windowed_placement);
    SetWindowPos(s_window, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ApplyWindowedSize();  /* panel may have been toggled while fullscreen */
  }
  InvalidateRect(s_window, NULL, FALSE);
  UpdateDebugTitle();
}

/* File dialog for Save State As / Load State From. The snapshot itself is
 * taken at the next frame boundary in the main loop, same as F11/F12. */
static void PromptStatePath(int save_mode) {
  char path[1024] = "";
  OPENFILENAMEA ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = s_window;
  ofn.lpstrFilter = "Save states (*.state)\0*.state\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof path;
  ofn.lpstrDefExt = "state";
  /* OFN_NOCHANGEDIR: the dialog must not move the process CWD, which
   * relative evidence paths and quicksave.state depend on. */
  ofn.Flags = OFN_NOCHANGEDIR |
              (save_mode ? OFN_OVERWRITEPROMPT
                         : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
  BOOL accepted = save_mode ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
  if (!accepted) return;
  snprintf(s_pending_state_path, sizeof s_pending_state_path, "%s", path);
  s_pending_state_op = save_mode ? 1 : 2;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
      s_running = 0;
      PostQuitMessage(0);
      return 0;
    case WM_UAHDRAWMENU:
      if (DrawDarkMenuBarBackground(hwnd, (const UahMenu *)lp)) return 1;
      break;
    case WM_UAHDRAWMENUITEM:
      if (DrawDarkMenuBarItem((const UahDrawMenuItem *)lp)) return 1;
      break;
    case WM_NCPAINT:
    case WM_NCACTIVATE: {
      LRESULT result = DefWindowProc(hwnd, msg, wp, lp);
      if (s_menu && !s_fullscreen) PaintOverMenuBarLine(hwnd);
      return result;
    }
    case WM_KILLFOCUS:
      /* Never carry a held SNES button across a focus transition.  Global
       * asynchronous polling could leave a button latched when the desktop
       * shell or an automation window consumed the matching key-up. */
      s_manual_input = 0;
      s_last_input = 0;
      return 0;
    case WM_KEYUP: {
      uint32_t bit = InputBitForVirtualKey(wp);
      if (bit) {
        s_manual_input &= ~bit;
        return 0;
      }
      break;
    }
    case WM_KEYDOWN: {
      uint32_t bit = InputBitForVirtualKey(wp);
      if (bit) {
        s_manual_input |= bit;
        return 0;
      }
      if (lp & (1u << 30)) return 0;  /* ignore key-repeat toggles */
      if (wp == VK_ESCAPE) {
        if (s_fullscreen) {
          SetFullscreen(0);
        } else {
          s_running = 0;
          PostQuitMessage(0);
        }
      } else if (wp == VK_F1) {
        Dkc1DebugSetProvenanceOverlay(!Dkc1DebugProvenanceOverlay());
        UpdateDebugTitle();
      } else if (wp == VK_F2) {
        Dkc1DebugSetLayerMask(0xff);
        UpdateDebugTitle();
      } else if (wp >= VK_F3 && wp <= VK_F6) {
        static const uint8_t masks[] = {0x01, 0x02, 0x04, 0x10};
        Dkc1DebugSetLayerMask(masks[wp - VK_F3]);
        UpdateDebugTitle();
      } else if (wp == VK_F7) {
        s_paused = !s_paused;
        s_step_once = 0;
        snprintf(s_host_status, sizeof s_host_status,
                 "%s by user", s_paused ? "paused" : "resumed");
        UpdateDebugTitle();
      } else if (wp == VK_F8 && s_paused) {
        s_step_once = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "single frame requested");
      } else if (wp == VK_F9) {
        s_export_requested = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "repro bundle export requested");
      } else if (wp == VK_F11) {
        s_quicksave_requested = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "quick save requested");
      } else if (wp == VK_F12) {
        s_quickload_requested = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "quick load requested");
      }
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int click_x = (int)(short)LOWORD(lp);
      const int click_y = (int)(short)HIWORD(lp);
      int game_x = -1, game_y = -1;
      if (s_fullscreen) {
        RECT client;
        GetClientRect(hwnd, &client);
        const int cw = client.right, ch = client.bottom;
        if (cw > 0 && ch > 0) {
          int dw = cw, dh = cw * s_height / s_width;
          if (dh > ch) { dh = ch; dw = ch * s_width / s_height; }
          const int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
          if (click_x >= dx && click_x < dx + dw && click_y >= dy &&
              click_y < dy + dh) {
            game_x = (click_x - dx) * s_width / dw;
            game_y = (click_y - dy) * s_height / dh;
          }
        }
      } else if (click_x >= 0 && click_x < s_width * kScale &&
                 click_y >= 0 && click_y < s_height * kScale) {
        game_x = click_x / kScale;
        game_y = click_y / kScale;
      }
      if (game_x >= 0) {
        s_inspect_x = game_x;
        s_inspect_y = game_y;
        s_inspect_pending = 2; /* resolve after a provenance-armed render */
        WsShadowDebugSetProvenanceEnabled(true);
        if (s_paused) /* paused loop repaints but never re-renders */
          s_inspect_pending = 1;
      }
      return 0;
    }
    case WM_SYSKEYDOWN:
      if (wp == VK_RETURN) {  /* Alt+Enter toggles fullscreen */
        if (!(lp & (1u << 30))) SetFullscreen(!s_fullscreen);
        return 0;
      }
      break;
    case WM_SYSCHAR:
      if (wp == VK_RETURN) return 0;  /* no menu beep for Alt+Enter */
      break;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case kMenuQuickSave:
          s_quicksave_requested = 1;
          snprintf(s_host_status, sizeof s_host_status,
                   "quick save requested");
          break;
        case kMenuQuickLoad:
          s_quickload_requested = 1;
          snprintf(s_host_status, sizeof s_host_status,
                   "quick load requested");
          break;
        case kMenuSaveStateAs:
          PromptStatePath(1);
          break;
        case kMenuLoadStateFrom:
          PromptStatePath(0);
          break;
        case kMenuExportRepro:
          s_export_requested = 1;
          snprintf(s_host_status, sizeof s_host_status,
                   "repro bundle export requested");
          break;
        case kMenuExit:
          s_running = 0;
          PostQuitMessage(0);
          break;
        case kMenuPauseResume:
          s_paused = !s_paused;
          s_step_once = 0;
          snprintf(s_host_status, sizeof s_host_status,
                   "%s by user", s_paused ? "paused" : "resumed");
          break;
        case kMenuSingleStep:
          if (s_paused) {
            s_step_once = 1;
            snprintf(s_host_status, sizeof s_host_status,
                     "single frame requested");
          }
          break;
        case kMenuFullscreen:
          SetFullscreen(!s_fullscreen);
          break;
        case kMenuTogglePanel:
          s_panel_enabled = !s_panel_enabled;
          ApplyWindowedSize();
          break;
        case kMenuProvenance:
          Dkc1DebugSetProvenanceOverlay(!Dkc1DebugProvenanceOverlay());
          break;
        case kMenuFpsCounter:
          s_show_fps = !s_show_fps;
          break;
        case kMenuAspectNative:
          SetAspectMode(0);
          break;
        case kMenuAspectWidescreen:
          SetAspectMode(1);
          break;
        case kMenuLayerComposite: Dkc1DebugSetLayerMask(0xff); break;
        case kMenuLayerBg1: Dkc1DebugSetLayerMask(0x01); break;
        case kMenuLayerBg2: Dkc1DebugSetLayerMask(0x02); break;
        case kMenuLayerBg3: Dkc1DebugSetLayerMask(0x04); break;
        case kMenuLayerObj: Dkc1DebugSetLayerMask(0x10); break;
        default:
          return DefWindowProc(hwnd, msg, wp, lp);
      }
      UpdateDebugTitle();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      PresentFrame(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

static uint32_t PollInput(void) {
  if (GetForegroundWindow() != s_window) {
    s_manual_input = 0;
    return 0;
  }

  /* WM_KEYUP can be lost around focus/desktop transitions.  Treat the
   * physical high bit as authoritative on every frame so a missed message
   * can never turn a short tap into a permanently held SNES button.  Keep
   * the message-owned copy for immediate UI bookkeeping, but reconcile it
   * here before the controller snapshot is handed to the guest. */
  uint32_t physical = 0;
  if (GetAsyncKeyState('Z') & 0x8000) physical |= 0x001;
  if (GetAsyncKeyState('X') & 0x8000) physical |= 0x002;
  if (GetAsyncKeyState(VK_RSHIFT) & 0x8000) physical |= 0x004;
  if (GetAsyncKeyState(VK_RETURN) & 0x8000) physical |= 0x008;
  if (GetAsyncKeyState(VK_UP) & 0x8000) physical |= 0x010;
  if (GetAsyncKeyState(VK_DOWN) & 0x8000) physical |= 0x020;
  if (GetAsyncKeyState(VK_LEFT) & 0x8000) physical |= 0x040;
  if (GetAsyncKeyState(VK_RIGHT) & 0x8000) physical |= 0x080;
  if (GetAsyncKeyState('S') & 0x8000) physical |= 0x100;
  if (GetAsyncKeyState('A') & 0x8000) physical |= 0x200;
  if (GetAsyncKeyState('Q') & 0x8000) physical |= 0x400;
  if (GetAsyncKeyState('W') & 0x8000) physical |= 0x800;
  s_manual_input = physical;
  return physical;
}

static void AudioInit(void) {
  WAVEFORMATEX format;
  memset(&format, 0, sizeof format);
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 2;
  format.nSamplesPerSec = 32040;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 4;
  format.nAvgBytesPerSec = 32040 * 4;
  {
    const char *override = getenv("DKC1_AUDIO_PREROLL");
    if (override && *override) {
      const int parsed = atoi(override);
      if (parsed >= 1 && parsed <= 4)
        s_audio_preroll_buffers = parsed;
    }
  }
  if (waveOutOpen(&s_waveout, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)
      != MMSYSERR_NOERROR) {
    s_waveout = NULL;
    return;
  }
  RtlSetAudioOutputRate(format.nSamplesPerSec);
  /* The first device block is not submitted until the engine's native ring
   * reaches its normal occupancy target.  Starting consumption immediately
   * used to create dozens of silent underflows during every launch. */
  waveOutPause(s_waveout);
  for (int i = 0; i < kAudioBuffers; i++) {
    s_wave_headers[i].lpData = (LPSTR)s_wave_data[i];
    s_wave_headers[i].dwBufferLength = sizeof s_wave_data[i];
    waveOutPrepareHeader(s_waveout, &s_wave_headers[i],
                         sizeof s_wave_headers[i]);
    s_wave_headers[i].dwFlags |= WHDR_DONE;
  }
}

static int AudioPendingFrames(void) {
  int pending = 0;
  for (int i = 0; i < kAudioBuffers; i++) {
    if (!(s_wave_headers[i].dwFlags & WHDR_DONE))
      pending += (int)(s_wave_headers[i].dwBufferLength / 4);
  }
  return pending;
}

static int AudioPendingBuffers(void) {
  int pending = 0;
  for (int i = 0; i < kAudioBuffers; i++)
    if (!(s_wave_headers[i].dwFlags & WHDR_DONE)) pending++;
  return pending;
}

static void AudioResetTimeline(void) {
  if (!s_waveout) return;
  /* Host buffers describe the old timeline and must never survive a state
   * load. waveOutReset returns every prepared header with WHDR_DONE set. */
  waveOutReset(s_waveout);
  waveOutPause(s_waveout);
  s_wave_index = 0;
  s_audio_accumulator = 0.0;
  s_audio_started = 0;
  s_audio_last_queued_frames = 0;
  /* A snapshot normally contains an already-warm native audio ring. Preserve
   * that producer/consumer relationship: waiting another four blocks would
   * exceed the engine's 250 ms consumer-presence window. Only a genuinely
   * cold loaded state needs to produce one complete source block first. */
  AudioTraceStats stats;
  audio_trace_get_stats(&stats);
  s_audio_ring_frames = stats.occupancy_current;
  s_audio_internal_underflows = stats.output_underflows;
  s_audio_ring_start_threshold = kAudioFramesPerBuffer + 2;
  s_audio_waiting_for_ring =
      stats.occupancy_current < s_audio_ring_start_threshold;
}

static void AudioPump(void) {
  if (!s_waveout) return;
  AudioTraceStats audio_stats;
  if (s_audio_waiting_for_ring || s_audio_log_stats) {
    audio_trace_get_stats(&audio_stats);
    s_audio_ring_frames = audio_stats.occupancy_current;
    s_audio_internal_underflows = audio_stats.output_underflows;
    if (s_audio_waiting_for_ring) {
      if (audio_stats.occupancy_current < s_audio_ring_start_threshold) return;
      s_audio_waiting_for_ring = 0;
    }
  }
  int pending_before = AudioPendingFrames();
  if (s_audio_started && pending_before == 0) {
    /* A pause, state-load stall, or external scheduler hitch drained the
     * device. Re-enter preroll instead of resuming with another one-block
     * knife edge and a click at the gap boundary. */
    s_audio_starvations++;
    waveOutPause(s_waveout);
    s_audio_started = 0;
    s_audio_ring_start_threshold = kAudioFramesPerBuffer + 2;
    s_audio_waiting_for_ring = 1;
    s_audio_accumulator = 0.0;
    return;
  }
  s_audio_accumulator += 32040.0 / s_host_frame_rate;
  int frames = (int)s_audio_accumulator;
  s_audio_accumulator -= frames;
  if (frames <= 0) return;
  if (frames > kAudioFramesPerBuffer) frames = kAudioFramesPerBuffer;
  WAVEHDR *header = &s_wave_headers[s_wave_index];
  if (!(header->dwFlags & WHDR_DONE)) {
    s_audio_drops++;
    return;  /* device is behind; drop this frame's audio */
  }
  int16_t *samples = (int16_t *)header->lpData;
  memset(samples, 0, (size_t)frames * 4);
  RtlRenderAudio(samples, frames, 2);
  if (s_audio_log_stats) {
    audio_trace_get_stats(&audio_stats);
    s_audio_ring_frames = audio_stats.occupancy_current;
    s_audio_internal_underflows = audio_stats.output_underflows;
  }
  header->dwBufferLength = (DWORD)frames * 4;
  if (waveOutWrite(s_waveout, header, sizeof *header) != MMSYSERR_NOERROR) {
    s_audio_drops++;
    return;
  }
  s_wave_index = (s_wave_index + 1) % kAudioBuffers;
  s_audio_last_queued_frames = AudioPendingFrames();
  if (!s_audio_started &&
      AudioPendingBuffers() >= s_audio_preroll_buffers) {
    if (waveOutRestart(s_waveout) == MMSYSERR_NOERROR)
      s_audio_started = 1;
  }
}

/* Present-time scheduler.  The old loop submitted the GDI frame first and
 * slept afterward.  Any variation in emulation/render work therefore moved
 * the next submission by the same amount, and a missed deadline was followed
 * by a short catch-up frame.  Schedule the presentation itself instead: do
 * the work, wait on an absolute QPC cadence, then submit.  Overruns re-anchor
 * immediately so a hitch is never followed by a burst of tightly-spaced
 * frames. */
typedef struct HostFramePacer {
  LARGE_INTEGER frequency;
  HANDLE timer;
  HMODULE dwm_module;
  HRESULT (WINAPI *dwm_timing)(HWND, DWM_TIMING_INFO *);
  double refresh_hz;
  double display_hz;
  double period_ticks;
  double submit_lead_ticks;
  double next_present_tick;
  double last_submit_tick;
  double last_present_tick;
  double pending_submit_tick;
  double pending_submit_interval_ms;
  double pending_submit_error_ms;
  double pending_work_ms;
  double pending_wait_ms;
  double pending_late_ms;
  double pending_setup_ms;
  double pending_emulation_ms;
  double pending_render_ms;
  double pending_diagnostics_ms;
  double pending_audio_ms;
  unsigned long overruns;
  unsigned long frames;
  long test_stall_frame;
  DWORD test_stall_ms;
  int test_stall_fired;
  int timer_resolution_active;
  int compositor_synced;
  const char *clock_source;
  FILE *log;
} HostFramePacer;

static int HostFramePacerOverrideHz(double *refresh_hz) {
  const char *override = getenv("DKC1_PRESENT_HZ");
  if (override && *override) {
    char *end = NULL;
    const double parsed = strtod(override, &end);
    if (end && !*end && parsed >= 30.0 && parsed <= 240.0) {
      *refresh_hz = parsed;
      return 1;
    }
  }
  return 0;
}

static double HostFramePacerFallbackHz(void) {
  /* An exact SNES cadence drifts through a 60 Hz compositor and produces a
   * periodic doubled/dropped presentation.  Prefer an exact display divisor
   * only when it remains effectively 60 Hz; unusual refresh rates retain the
   * hardware cadence rather than changing game speed substantially. */
  HDC dc = GetDC(s_window);
  const int display_hz = dc ? GetDeviceCaps(dc, VREFRESH) : 0;
  if (dc) ReleaseDC(s_window, dc);
  if (display_hz > 0) {
    const int divisor = (display_hz + 30) / 60;
    if (divisor > 0) {
      const double divided_hz = (double)display_hz / (double)divisor;
      if (divided_hz >= 59.5 && divided_hz <= 60.5)
        return divided_hz;
    }
  }
  return 60.098811862;
}

static int HostFramePacerReadDwm(HostFramePacer *pacer,
                                 DWM_TIMING_INFO *timing) {
  if (!pacer->dwm_timing) return 0;
  memset(timing, 0, sizeof *timing);
  timing->cbSize = sizeof *timing;
  /* The desktop-wide query exposes the composition clock even before this
   * GDI window has accumulated per-window present statistics. */
  return SUCCEEDED(pacer->dwm_timing(NULL, timing)) &&
         timing->qpcRefreshPeriod > 0;
}

static void HostFramePacerAnchorToDwm(HostFramePacer *pacer,
                                      double now_tick) {
  DWM_TIMING_INFO timing;
  if (!pacer->compositor_synced ||
      !HostFramePacerReadDwm(pacer, &timing)) {
    pacer->next_present_tick = now_tick;
    return;
  }
  const double display_period = (double)timing.qpcRefreshPeriod;
  int divisor = (int)(pacer->period_ticks / display_period + 0.5);
  if (divisor < 1) divisor = 1;
  /* WaitForPresent adds one game period. Back the stored anchor up by the
   * remaining display refreshes so its first target is the next vblank, then
   * continue at the selected integer divisor. Submit one millisecond before
   * that boundary to leave DWM time to consume the GDI surface. */
  pacer->next_present_tick = (double)timing.qpcVBlank -
      pacer->submit_lead_ticks - (double)(divisor - 1) * display_period;
  while (pacer->next_present_tick + pacer->period_ticks <= now_tick)
    pacer->next_present_tick += pacer->period_ticks;
}

static int HostFramePacerInit(HostFramePacer *pacer) {
  LARGE_INTEGER now;
  memset(pacer, 0, sizeof *pacer);
  if (!QueryPerformanceFrequency(&pacer->frequency) ||
      !QueryPerformanceCounter(&now))
    return 0;
  pacer->clock_source = "hardware";
  if (HostFramePacerOverrideHz(&pacer->refresh_hz)) {
    pacer->clock_source = "override";
  } else {
    pacer->dwm_module = LoadLibraryA("dwmapi.dll");
    if (pacer->dwm_module) {
      pacer->dwm_timing = (HRESULT (WINAPI *)(HWND, DWM_TIMING_INFO *))
          GetProcAddress(pacer->dwm_module, "DwmGetCompositionTimingInfo");
    }
    DWM_TIMING_INFO timing;
    if (HostFramePacerReadDwm(pacer, &timing)) {
      pacer->display_hz = (double)pacer->frequency.QuadPart /
                          (double)timing.qpcRefreshPeriod;
      const int divisor = (int)(pacer->display_hz / 60.0 + 0.5);
      const double divided_hz = divisor > 0
          ? pacer->display_hz / (double)divisor : 0.0;
      if (divided_hz >= 59.5 && divided_hz <= 60.5) {
        pacer->refresh_hz = divided_hz;
        pacer->period_ticks =
            (double)timing.qpcRefreshPeriod * (double)divisor;
        pacer->submit_lead_ticks =
            (double)pacer->frequency.QuadPart / 1000.0;
        pacer->compositor_synced = 1;
        pacer->clock_source = "dwm";
      }
    }
    if (!pacer->refresh_hz)
      pacer->refresh_hz = HostFramePacerFallbackHz();
  }
  s_host_frame_rate = pacer->refresh_hz;
  {
    const char *frame_text = getenv("DKC1_PACING_TEST_STALL_FRAME");
    const char *ms_text = getenv("DKC1_PACING_TEST_STALL_MS");
    if (frame_text && *frame_text && ms_text && *ms_text) {
      const long frame = strtol(frame_text, NULL, 10);
      const unsigned long milliseconds = strtoul(ms_text, NULL, 10);
      if (frame > 0 && milliseconds > 0 && milliseconds <= 1000) {
        pacer->test_stall_frame = frame;
        pacer->test_stall_ms = (DWORD)milliseconds;
      }
    }
  }
  if (!pacer->period_ticks)
    pacer->period_ticks =
        (double)pacer->frequency.QuadPart / pacer->refresh_hz;
  HostFramePacerAnchorToDwm(pacer, (double)now.QuadPart);
  pacer->timer = CreateWaitableTimerExA(
      NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!pacer->timer) {
    pacer->timer = CreateWaitableTimerA(NULL, FALSE, NULL);
    if (timeBeginPeriod(1) == TIMERR_NOERROR)
      pacer->timer_resolution_active = 1;
  }
  {
    const char *path = getenv("DKC1_PACING_LOG");
    if (path && *path) {
      pacer->log = fopen(path, "wb");
      if (pacer->log) {
        s_audio_log_stats = 1;
        fprintf(pacer->log,
                "{\"schema\":\"dkc1.pacing.v3\",\"refresh_hz\":%.9f,"
                "\"display_hz\":%.9f,\"clock_source\":\"%s\","
                "\"submit_lead_ms\":%.4f,\"audio_preroll\":%d,"
                "\"audio_ring_start_frames\":%d,"
                "\"test_stall_frame\":%ld,\"test_stall_ms\":%lu}\n",
                pacer->refresh_hz, pacer->display_hz,
                pacer->clock_source,
                pacer->submit_lead_ticks * 1000.0 /
                    (double)pacer->frequency.QuadPart,
                s_audio_preroll_buffers, kAudioRingStartFrames,
                pacer->test_stall_frame,
                (unsigned long)pacer->test_stall_ms);
      }
    }
  }
  return 1;
}

static void HostFramePacerInjectTestStall(HostFramePacer *pacer,
                                           long host_frame) {
  if (!pacer->test_stall_fired && pacer->test_stall_ms &&
      host_frame == pacer->test_stall_frame) {
    pacer->test_stall_fired = 1;
    Sleep(pacer->test_stall_ms);
  }
}

static void HostFramePacerReset(HostFramePacer *pacer) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  HostFramePacerAnchorToDwm(pacer, (double)now.QuadPart);
  pacer->last_submit_tick = 0.0;
  pacer->last_present_tick = 0.0;
}

static void HostFramePacerWaitForPresent(HostFramePacer *pacer,
                                         double work_start_tick) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double before_wait = (double)now.QuadPart;
  double target = pacer->next_present_tick + pacer->period_ticks;
  pacer->pending_work_ms =
      (before_wait - work_start_tick) * 1000.0 /
      (double)pacer->frequency.QuadPart;
  pacer->pending_late_ms = 0.0;

  if (before_wait > target) {
    pacer->pending_late_ms =
        (before_wait - target) * 1000.0 /
        (double)pacer->frequency.QuadPart;
    pacer->overruns++;
    if (pacer->compositor_synced) {
      const double late_ticks = before_wait - target;
      if (late_ticks > pacer->submit_lead_ticks) {
        /* The vblank itself has passed. Skip to the next compositor boundary
         * rather than permanently shifting the cadence off-phase. */
        HostFramePacerAnchorToDwm(pacer, before_wait);
        target = pacer->next_present_tick + pacer->period_ticks;
      }
      /* If only the pre-vblank lead was missed, submit immediately; DWM still
       * has the remainder of the lead window and the next target stays on the
       * original phase. */
    } else {
      /* No compositor clock is available. Do not chase the missed deadline;
       * give the next frame a complete interval. */
      target = before_wait;
    }
  }
  pacer->next_present_tick = target;

  for (;;) {
    QueryPerformanceCounter(&now);
    const double remaining = target - (double)now.QuadPart;
    if (remaining <= 0.0) break;
    const double remaining_ms =
        remaining * 1000.0 / (double)pacer->frequency.QuadPart;
    if (pacer->timer && remaining_ms > 1.25) {
      /* Wake one millisecond before the deadline, then spin only for the
       * bounded tail.  Waiting closer to the target exposed scheduler wake
       * jitter near one millisecond on an otherwise idle machine. */
      double coarse_ms = remaining_ms - 1.0;
      LARGE_INTEGER due;
      due.QuadPart = -(LONGLONG)(coarse_ms * 10000.0);
      if (!due.QuadPart) due.QuadPart = -1;
      if (SetWaitableTimer(pacer->timer, &due, 0, NULL, NULL, FALSE)) {
        WaitForSingleObject(pacer->timer, INFINITE);
        continue;
      }
    }
    YieldProcessor();
  }
  QueryPerformanceCounter(&now);
  pacer->pending_wait_ms =
      ((double)now.QuadPart - before_wait) * 1000.0 /
      (double)pacer->frequency.QuadPart;
}

static void HostFramePacerBeginPresent(HostFramePacer *pacer) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  pacer->pending_submit_tick = (double)now.QuadPart;
  pacer->pending_submit_interval_ms = pacer->last_submit_tick > 0.0
      ? (pacer->pending_submit_tick - pacer->last_submit_tick) * 1000.0 /
            (double)pacer->frequency.QuadPart
      : 0.0;
  pacer->pending_submit_error_ms =
      (pacer->pending_submit_tick - pacer->next_present_tick) * 1000.0 /
      (double)pacer->frequency.QuadPart;
  pacer->last_submit_tick = pacer->pending_submit_tick;
}

static double HostFramePacerPhaseMs(HostFramePacer *pacer,
                                    double *previous_tick) {
  if (!pacer->log) return 0.0;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double current = (double)now.QuadPart;
  const double elapsed = (current - *previous_tick) * 1000.0 /
                         (double)pacer->frequency.QuadPart;
  *previous_tick = current;
  return elapsed;
}

static void HostFramePacerPresented(HostFramePacer *pacer, long host_frame) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double present_tick = (double)now.QuadPart;
  const double interval_ms = pacer->last_present_tick > 0.0
      ? (present_tick - pacer->last_present_tick) * 1000.0 /
            (double)pacer->frequency.QuadPart
      : 0.0;
  const double present_ms =
      (present_tick - pacer->pending_submit_tick) * 1000.0 /
      (double)pacer->frequency.QuadPart;
  pacer->last_present_tick = present_tick;
  pacer->frames++;
  if (pacer->log) {
    fprintf(pacer->log,
            "{\"frame\":%ld,\"work_ms\":%.4f,\"wait_ms\":%.4f,"
            "\"late_ms\":%.4f,\"present_interval_ms\":%.4f,"
            "\"submit_interval_ms\":%.4f,\"submit_error_ms\":%.4f,"
            "\"present_ms\":%.4f,\"setup_ms\":%.4f,"
            "\"emulation_ms\":%.4f,\"render_ms\":%.4f,"
            "\"diagnostics_ms\":%.4f,\"audio_ms\":%.4f,"
            "\"audio_queued_frames\":%d,\"audio_starvations\":%lu,"
            "\"audio_drops\":%lu,\"audio_ring_frames\":%lu,"
            "\"audio_internal_underflows\":%llu,\"overruns\":%lu}\n",
            host_frame, pacer->pending_work_ms, pacer->pending_wait_ms,
            pacer->pending_late_ms, interval_ms,
            pacer->pending_submit_interval_ms,
            pacer->pending_submit_error_ms, present_ms,
            pacer->pending_setup_ms, pacer->pending_emulation_ms,
            pacer->pending_render_ms, pacer->pending_diagnostics_ms,
            pacer->pending_audio_ms, s_audio_last_queued_frames,
            s_audio_starvations, s_audio_drops, s_audio_ring_frames,
            s_audio_internal_underflows,
            pacer->overruns);
  }
}

static void HostFramePacerClose(HostFramePacer *pacer) {
  if (pacer->log) {
    fflush(pacer->log);
    fclose(pacer->log);
    s_audio_log_stats = 0;
  }
  if (pacer->timer) CloseHandle(pacer->timer);
  if (pacer->timer_resolution_active) timeEndPeriod(1);
  if (pacer->dwm_module) FreeLibrary(pacer->dwm_module);
}

int main(int argc, char **argv) {
  InitBuildIdentity();
  /* Contain default-named tier2 discovery captures instead of littering
   * the working directory; explicit env settings are respected. */
  if (!getenv("SNESRECOMP_TIER2_DIR") && !getenv("SNESRECOMP_TIER2_MANIFEST")) {
    _mkdir("build");
    _mkdir("build/tier2");
    _putenv("SNESRECOMP_TIER2_DIR=build/tier2");
  }
  Dkc1FlightRecorderSetBuildInfo(s_build_id);
  const char *rom_path = argc > 1 ? argv[1] : "dkc1.sfc";
  snprintf(s_rom_path, sizeof s_rom_path, "%s", rom_path);
  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc1ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    char message[320];
    snprintf(message, sizeof message,
             "usage: dkc1_desktop <rom.sfc>\n\n%s: %s", rom_error, rom_path);
    MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
    return 2;
  }

  {
    const char *widescreen_text = getenv("DKC1_WIDESCREEN");
    Dkc1VideoSetWidescreen(
        !(widescreen_text && *widescreen_text == '0'));  /* default on */
    {
      /* Level-wall presentation: glide (default), reflect, bars, or shift. */
      const char *edge_text = getenv("DKC1_WIDESCREEN_EDGE");
      Dkc1EdgePolicy edge_policy;
      if (edge_text && *edge_text &&
          Dkc1EdgePolicyFromName(edge_text, &edge_policy))
        Dkc1VideoSetEdgePolicy(edge_policy);
    }
  }
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    MessageBoxA(NULL, "snesrecomp rejected the verified ROM", "DKC1Recomp",
                MB_ICONERROR);
    return 4;
  }

  {
    const char *panel_text = getenv("DKC1_DESKTOP_DEBUG_PANEL");
    s_panel_enabled = !(panel_text && *panel_text == '0');
  }
  s_paused = EnvironmentEnabled("DKC1_START_PAUSED") ? 1 : 0;
  {
    const char *autoclose = getenv("DKC1_ROUTE_AUTOCLOSE_MS");
    if (autoclose && *autoclose) {
      unsigned long parsed = strtoul(autoclose, NULL, 10);
      if (parsed > 60000ul) parsed = 60000ul;
      s_route_autoclose_ms = (DWORD)parsed;
    }
  }
  {
    const char *limit = getenv("DKC1_ROUTE_FRAME_LIMIT");
    if (limit && *limit) {
      long parsed = strtol(limit, NULL, 10);
      if (parsed > 0) s_route_frame_limit = parsed;
    }
  }
  {
    const char *snapshot = getenv("DKC1_SAVESTATE_INPUT");
    if (snapshot && *snapshot) {
      if (!RtlLoadSnapshot(snapshot)) {
        char message[1024];
        snprintf(message, sizeof message,
                 "Unable to load native snapshot:\n%s", snapshot);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      /* Automation path: never modal, but never silent either. */
      char commit[80];
      if (StateBuildCommit(snapshot, commit, sizeof commit) &&
          strcmp(commit, DKC1_BUILD_COMMIT) != 0) {
        snprintf(s_host_status, sizeof s_host_status,
                 "WARNING: state from build %.32s, this is %s",
                 commit, DKC1_BUILD_COMMIT);
        fprintf(stderr, "state_build_mismatch state=%s this=%s\n", commit,
                DKC1_BUILD_COMMIT);
      }
    }
  }
  {
    const char *bundle = getenv("DKC1_SUPERZSNES_STATE");
    const char *snapshot = getenv("DKC1_SAVESTATE_INPUT");
    if (bundle && *bundle) {
      char import_error[256];
      if ((snapshot && *snapshot) ||
          !Dkc1ImportSuperZsnesState(bundle, import_error,
                                     sizeof import_error)) {
        char message[768];
        snprintf(message, sizeof message,
                 "Unable to import SuperZSNES state:\n%s\n\n%s",
                 bundle,
                 (snapshot && *snapshot)
                     ? "DKC1_SAVESTATE_INPUT and DKC1_SUPERZSNES_STATE are mutually exclusive"
                     : import_error);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      snprintf(s_host_status, sizeof s_host_status,
               "imported SuperZSNES frame %d; audio history reconstructed",
               snes_frame_counter);
    }
  }
  {
    const char *playback_path = getenv("SNESRECOMP_INPUT_PLAY");
    if (playback_path && *playback_path) {
      char error[256];
      if (!Dkc1InputPlaybackLoad(playback_path, &s_input_playback,
                                 error, sizeof error)) {
        char message[768];
        snprintf(message, sizeof message, "Input playback failed:\n%s\n%s",
                 playback_path, error);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      snprintf(s_host_status, sizeof s_host_status,
               "loaded input playback: %s", playback_path);
    }
  }
  {
    const char *script_path = getenv("DKC1_SCRIPT");
    if (script_path && *script_path) {
      char error[256];
      if (s_input_playback.count) {
        MessageBoxA(NULL,
                    "DKC1_SCRIPT and SNESRECOMP_INPUT_PLAY are mutually exclusive",
                    "DKC1Recomp", MB_ICONERROR);
        Dkc1InputPlaybackFree(&s_input_playback);
        free(rom);
        return 20;
      }
      if (!Dkc1ScriptLoad(script_path, error, sizeof error)) {
        char message[768];
        snprintf(message, sizeof message, "Route script failed:\n%s\n%s",
                 script_path, error);
        MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
        free(rom);
        return 20;
      }
      s_script_loaded = 1;
      snprintf(s_host_status, sizeof s_host_status,
               "loaded route: %s", script_path);
    }
  }
  {
    char error[256];
    int opened = Dkc1WramDumpOpenFromEnvironment(&s_wram_dump,
                                                  error, sizeof error);
    if (opened < 0) {
      char message[512];
      snprintf(message, sizeof message, "WRAM dump setup failed:\n%s", error);
      MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
      Dkc1ScriptFree();
      Dkc1InputPlaybackFree(&s_input_playback);
      free(rom);
      return 20;
    }
  }
  {
    char error[256];
    int armed = Dkc1FlightRecorderInitialize(error, sizeof error);
    if (armed < 0) {
      char message[512];
      snprintf(message, sizeof message,
               "Flight recorder setup failed:\n%s", error);
      MessageBoxA(NULL, message, "DKC1Recomp", MB_ICONERROR);
      (void)Dkc1WramDumpClose(&s_wram_dump, NULL, 0);
      Dkc1ScriptFree();
      Dkc1InputPlaybackFree(&s_input_playback);
      free(rom);
      return 20;
    }
  }

  s_width = Dkc1VideoWidth();
  s_height = kDkc1VideoHeight;
  Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);
  /* A diagnostic snapshot may be launched paused. Render its exact machine
   * state without advancing CPU/APU/PPU time so the first visible window is
   * useful immediately and the user does not have to race an F7 keypress. */
  if (s_paused)
    Dkc1DrawPpuFrame();

  EnableDarkMenus();

  WNDCLASSA wc;
  memset(&wc, 0, sizeof wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(DKC1_DARK_CLIENT);
  wc.lpszClassName = "DKC1RecompWindow";
  RegisterClassA(&wc);

  s_menu = BuildMenuBar();
  RECT rect = { 0, 0,
                s_width * kScale + (s_panel_enabled ? kPanelWidth : 0),
                s_height * kScale };
  AdjustWindowRect(&rect, kWindowedStyle, TRUE);
  s_window = CreateWindowA(
      wc.lpszClassName,
      "DKC1Recomp — Z=B  X=Y  S=A  A=X  Q/W=L/R  Enter=Start  Esc=quit",
      kWindowedStyle | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      rect.right - rect.left, rect.bottom - rect.top,
      NULL, s_menu, wc.hInstance, NULL);
  EnableDarkTitleBar(s_window);
  SetWindowPos(s_window, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
  UpdateDebugTitle();

  memset(&s_bmi, 0, sizeof s_bmi);
  s_bmi.bmiHeader.biSize = sizeof s_bmi.bmiHeader;
  s_bmi.bmiHeader.biWidth = s_width;
  s_bmi.bmiHeader.biHeight = -s_height;  /* top-down */
  s_bmi.bmiHeader.biPlanes = 1;
  s_bmi.bmiHeader.biBitCount = 32;
  s_bmi.bmiHeader.biCompression = BI_RGB;

  AudioInit();

  HostFramePacer pacer;
  if (!HostFramePacerInit(&pacer)) {
    MessageBoxA(s_window, "high-resolution clock unavailable",
                "DKC1Recomp", MB_ICONERROR);
    free(rom);
    return 5;
  }
  LARGE_INTEGER freq;
  freq = pacer.frequency;

  while (s_running) {
    LARGE_INTEGER work_start;
    QueryPerformanceCounter(&work_start);
    double phase_tick = (double)work_start.QuadPart;
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!s_running) break;

    if (s_export_requested) {
      char bundle[1024], error[256];
      s_export_requested = 0;
      if (Dkc1FlightRecorderExport(s_host_frame, bundle, sizeof bundle,
                                   error, sizeof error)) {
        SpawnLayerCapture(bundle);
        if (s_auto_export_fired) {
          /* Keep recording: a post-failure tail shows how the failure
           * evolved, not just the moment it was detected. */
          snprintf(s_tail_bundle, sizeof s_tail_bundle, "%s", bundle);
          s_tail_export_frame = s_host_frame;
          s_tail_deadline = s_host_frame + 120;
        }
        s_auto_export_fired = 0;
        snprintf(s_host_status, sizeof s_host_status,
                 "repro exported (+layer captures): %.450s", bundle);
      } else
        snprintf(s_host_status, sizeof s_host_status,
                 "repro export failed: %.460s", error);
      UpdateDebugTitle();
    }

    /* Quick save/load: native full-machine snapshots, handled at the frame
     * boundary like script state ops. State v8 includes the sparse host-only
     * widescreen shadow and actor-phase decisions, while the recorder export
     * preserves the causal input history and same-frame raw planes. Older
     * v4-v7 states remain loadable and intentionally rebuild host history. */
    if (s_quicksave_requested) {
      char bundle[1024] = {0};
      char export_error[256] = {0};
      s_quicksave_requested = 0;
      const int saved = RtlSaveSnapshot("quicksave.state");
      if (saved)
        WriteStateBuildInfo("quicksave.state");
      const int exported = saved && Dkc1FlightRecorderEnabled() &&
          Dkc1FlightRecorderExport(s_host_frame, bundle, sizeof bundle,
                                   export_error, sizeof export_error);
      if (exported) {
        SpawnLayerCapture(bundle);
        snprintf(s_host_status, sizeof s_host_status,
                 "quick save + live repro (+layers): %.430s", bundle);
      } else if (saved && Dkc1FlightRecorderEnabled()) {
        snprintf(s_host_status, sizeof s_host_status,
                 "quick save OK; live repro FAILED: %.390s", export_error);
      } else {
        snprintf(s_host_status, sizeof s_host_status,
                 saved ? "quick save -> quicksave.state"
                       : "quick save FAILED");
      }
      UpdateDebugTitle();
    }
    if (s_quickload_requested) {
      s_quickload_requested = 0;
      if (!ConfirmStateBuildCompat("quicksave.state")) {
        snprintf(s_host_status, sizeof s_host_status,
                 "quick load declined (build mismatch)");
      } else {
        const int loaded = RtlLoadSnapshot("quicksave.state");
        if (loaded) AudioResetTimeline();
        char recorder_error[256];
        const int reanchored = !loaded ||
            Dkc1FlightRecorderReanchorAfterStateLoad(
                s_host_frame, recorder_error, sizeof recorder_error);
        snprintf(s_host_status, sizeof s_host_status,
                 !loaded ? "quick load FAILED (no quicksave.state?)"
                         : reanchored
                               ? "quick load <- quicksave.state"
                               : "quick load succeeded; recorder reanchor FAILED: %.180s",
                 recorder_error);
      }
      UpdateDebugTitle();
    }
    if (s_pending_state_op) {
      const int save_op = s_pending_state_op == 1;
      s_pending_state_op = 0;
      if (!save_op && !ConfirmStateBuildCompat(s_pending_state_path)) {
        snprintf(s_host_status, sizeof s_host_status,
                 "state load declined (build mismatch)");
        UpdateDebugTitle();
        continue;
      }
      const int accepted = save_op ? RtlSaveSnapshot(s_pending_state_path)
                                   : RtlLoadSnapshot(s_pending_state_path);
      if (!save_op && accepted) AudioResetTimeline();
      if (save_op && accepted)
        WriteStateBuildInfo(s_pending_state_path);
      char recorder_error[256];
      const int reanchored = save_op || !accepted ||
          Dkc1FlightRecorderReanchorAfterStateLoad(
              s_host_frame, recorder_error, sizeof recorder_error);
      if (!reanchored) {
        snprintf(s_host_status, sizeof s_host_status,
                 "state load succeeded; recorder reanchor FAILED: %.180s",
                 recorder_error);
      } else {
        snprintf(s_host_status, sizeof s_host_status, "state %s %s %.400s",
                 save_op ? "save" : "load",
                 accepted ? (save_op ? "->" : "<-") : "FAILED:",
                 s_pending_state_path);
      }
      UpdateDebugTitle();
    }

    if (s_route_terminal_tick && s_route_autoclose_ms &&
        GetTickCount64() - s_route_terminal_tick >= s_route_autoclose_ms) {
      s_running = 0;
      continue;
    }

    if (s_paused && !s_step_once) {
      ResolvePixelInspect();
      HDC dc = GetDC(s_window);
      PresentFrame(dc);
      ReleaseDC(s_window, dc);
      HostFramePacerReset(&pacer);
      Sleep(16);
      continue;
    }

    Dkc1ScriptOps script_ops = {0};
    uint32_t input = 0;
    int run_frame = 1;
    if (s_route_frame_limit > 0 && s_host_frame >= s_route_frame_limit) {
      SetRouteTerminal(0, "complete",
                       "frame limit reached; paused for inspection");
      continue;
    }
    if (s_script_loaded) {
      if (Dkc1ScriptFinished()) {
        SetRouteTerminal(0, "complete",
                         "route complete; paused for inspection");
        continue;
      }
      bool failed = false;
      input = Dkc1ScriptNextInput(g_ram, &script_ops, &failed);
      if (failed) {
        SetRouteTerminal(1, "script_failed", Dkc1ScriptError());
        continue;
      }
      if (script_ops.state_load) {
        char message[512];
        char recorder_error[256];
        if (!RtlLoadSnapshot(script_ops.state_load)) {
          snprintf(message, sizeof message,
                   "unable to load snapshot: %.430s", script_ops.state_load);
          SetRouteTerminal(1, "state_load_failed", message);
          continue;
        }
        AudioResetTimeline();
        if (!Dkc1FlightRecorderReanchorAfterStateLoad(
                s_host_frame, recorder_error, sizeof recorder_error)) {
          snprintf(message, sizeof message,
                   "state loaded but recorder reanchor failed: %.380s",
                   recorder_error);
          SetRouteTerminal(1, "state_load_reanchor_failed", message);
          continue;
        }
      }
      if (script_ops.checkpoint &&
          !Dkc1DebugCheckpoint(script_ops.checkpoint, (int)s_host_frame)) {
        char message[512];
        snprintf(message, sizeof message,
                 "unable to record checkpoint: %.400s", script_ops.checkpoint);
        SetRouteTerminal(1, "checkpoint_failed", message);
        continue;
      }
      if (script_ops.state_save && !RtlSaveSnapshot(script_ops.state_save)) {
        char message[512];
        snprintf(message, sizeof message,
                 "unable to save snapshot: %.430s", script_ops.state_save);
        SetRouteTerminal(1, "state_save_failed", message);
        continue;
      }
      run_frame = script_ops.run_frame ? 1 : 0;
    } else if (s_input_playback.count) {
      if ((size_t)s_host_frame >= s_input_playback.count) {
        SetRouteTerminal(0, "complete",
                         "input playback complete; paused for inspection");
        continue;
      }
      input = Dkc1InputPlaybackFrame(&s_input_playback,
                                      (size_t)s_host_frame);
    } else {
      input = PollInput();
    }

    if (!run_frame) {
      UpdateDebugTitle();
      continue;
    }

    s_last_input = input;
    Dkc1DebugRecordInput(input);
    pacer.pending_setup_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    RtlRunFrame(input);
    pacer.pending_emulation_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    if (g_fail) {
      MessageBoxA(s_window, "runtime failure (off-rails execution)",
                  "DKC1Recomp", MB_ICONERROR);
      break;
    }
    if (!Dkc1LastLleResult()) {
      char message[128];
      snprintf(message, sizeof message,
               "execution stopped at $%06x", (unsigned)Dkc1ResumePc());
      MessageBoxA(s_window, message, "DKC1Recomp", MB_ICONERROR);
      break;
    }
    Dkc1DrawPpuFrame();
    pacer.pending_render_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    s_host_frame++;
    Dkc1BlankScanFrame(s_host_frame, s_pixels, s_width, s_height,
                       Dkc1VideoTerrainReady());
    Dkc1InvariantMonitorFrame(s_host_frame);
    ResolvePixelInspect();
    if (s_tail_bundle[0] && s_host_frame >= s_tail_deadline) {
      char tail_error[256];
      if (Dkc1FlightRecorderExportTail(s_tail_bundle, s_tail_export_frame,
                                       s_host_frame, tail_error,
                                       sizeof tail_error))
        snprintf(s_host_status, sizeof s_host_status,
                 "post-failure tail saved into bundle");
      else
        snprintf(s_host_status, sizeof s_host_status,
                 "post-tail failed: %.400s", tail_error);
      s_tail_bundle[0] = 0;
      UpdateDebugTitle();
    }
    MaybeAutoExport();
    {
      char error[256];
      if (!Dkc1WramDumpFrame(&s_wram_dump, s_host_frame,
                             snes_frame_counter, g_ram,
                             error, sizeof error)) {
        s_paused = 1;
        snprintf(s_host_status, sizeof s_host_status,
                 "WRAM dump failed: %.430s", error);
      }
    }
    Dkc1DebugDumpFrame((int)s_host_frame);
    Dkc1FlightRecorderRecord(s_host_frame, input);
    pacer.pending_diagnostics_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    AudioPump();
    pacer.pending_audio_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    HostFramePacerInjectTestStall(&pacer, s_host_frame);

    {
      /* Emulated-frame rate over a rolling half-second window. */
      static LARGE_INTEGER fps_anchor;
      static int fps_frames;
      fps_frames++;
      LARGE_INTEGER fps_now;
      QueryPerformanceCounter(&fps_now);
      if (!fps_anchor.QuadPart) fps_anchor = fps_now;
      const double elapsed =
          (double)(fps_now.QuadPart - fps_anchor.QuadPart) /
          (double)freq.QuadPart;
      if (elapsed >= 0.5) {
        s_fps_value = fps_frames / elapsed;
        fps_frames = 0;
        fps_anchor = fps_now;
      }
    }

    HostFramePacerWaitForPresent(&pacer, (double)work_start.QuadPart);
    HDC dc = GetDC(s_window);
    HostFramePacerBeginPresent(&pacer);
    PresentFrame(dc);
    ReleaseDC(s_window, dc);
    HostFramePacerPresented(&pacer, s_host_frame);
    if ((s_host_frame % 15) == 0) UpdateDebugTitle();
    s_step_once = 0;

  }

  if (s_waveout) {
    waveOutReset(s_waveout);
    waveOutClose(s_waveout);
  }
  {
    char error[256];
    if (!Dkc1WramDumpClose(&s_wram_dump, error, sizeof error))
      fprintf(stderr, "wram_dump: %s\n", error);
  }
  Dkc1DebugDumpClose();
  Dkc1FlightRecorderClose();
  if (s_script_loaded && !s_route_result_written)
    WriteRouteResult("aborted");
  Dkc1ScriptFree();
  Dkc1InputPlaybackFree(&s_input_playback);
  HostFramePacerClose(&pacer);
  free(rom);
  return 0;
}
