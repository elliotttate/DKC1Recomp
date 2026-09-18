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
#include "dkc1_dixie_mod.h"
#include "snes/dma.h"
#include "dkc1_invariant_monitor.h"
#include "dkc1_debug_dump.h"
#include "dkc1_flight_recorder.h"
#include "dkc1_framegen.h"
#include "win32_present_cadence.h"
#include "dkc1_script.h"
#include "dkc1_video.h"
#include "input_playback.h"
#include "verified_rom.h"
#include "wram_dump.h"
#include "desktop_sram.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "audio_trace.h"
#include "sha256.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

/* Interpreter telemetry for the pacing log (snes/interp_bridge.h pulls in
 * the bridge's internal types; the two accessors are all the host needs). */
long interp_tier_hit_count(void);
unsigned long long interp_bridge_steps_total(void);
const unsigned long long *interp_bridge_bank_steps(void);
const unsigned long long *interp_bridge_page_steps(void);

/* At exit, name the code pages the interpreter spent the most opcodes in.
 * Those are the recompiler coverage gaps that make some frames slow. */
static void ReportInterpreterHotspots(void) {
  const unsigned long long total = interp_bridge_steps_total();
  if (!total) return;
  const unsigned long long *pages = interp_bridge_page_steps();
  unsigned top_index[12] = {0};
  unsigned long long top_count[12] = {0};
  for (unsigned page = 0; page < 65536; page++) {
    const unsigned long long count = pages[page];
    if (!count || count <= top_count[11]) continue;
    int slot = 11;
    while (slot > 0 && top_count[slot - 1] < count) {
      top_count[slot] = top_count[slot - 1];
      top_index[slot] = top_index[slot - 1];
      slot--;
    }
    top_count[slot] = count;
    top_index[slot] = page;
  }
  fprintf(stderr, "[interp] %llu interpreted opcodes; hottest pages:",
          total);
  for (int slot = 0; slot < 12 && top_count[slot]; slot++)
    fprintf(stderr, " $%06X:%llu", top_index[slot] << 8, top_count[slot]);
  fprintf(stderr, "\n");
}

#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32_pacing_log.inc"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static Dkc1SramStore s_sram_store;
static int s_sram_error_reported;

enum {
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
  kMenuFrameGen,
  kMenuAspectNative,  /* kMenuAspect* stay contiguous for the radio group */
  kMenuAspectWidescreen,
  kMenuLayerComposite,  /* kMenuLayer* stay contiguous for the radio group */
  kMenuLayerBg1,
  kMenuLayerBg2,
  kMenuLayerBg3,
  kMenuLayerObj,
  kMenuToggleDixie,
  kMenuScalingSharp,  /* kMenuScaling* stay contiguous for the radio group */
  kMenuScalingNearest,
  kMenuScalingLinear,
  kMenuPixelAspectSnes,  /* kMenuPixelAspect* stay contiguous */
  kMenuPixelAspectSquare,
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
static int s_inspect_armed_provenance; /* click armed it; disarm on resolve */
static char s_pixel_report[640] = "(click any pixel)";
static int s_auto_export_fired;
static char s_tail_bundle[1024];
static long s_tail_export_frame;
static long s_tail_deadline;
static char s_host_status[512] = "manual play";
static Dkc1InputPlayback s_input_playback;
static Dkc1WramDump s_wram_dump;

/* Optional buffered pose smoothing; raw frames remain untouched for evidence.
 * s_smooth_pixels is delayed real F, s_mid_pixels is its following F+0.5.
 * The worker gets a private immutable midpoint copy for its half refresh. */
static uint8_t s_mid_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static uint8_t s_prev_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static uint8_t s_smooth_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];
static int s_smooth_valid;
static int s_prev_pixels_valid;
static int s_framegen_enabled;    /* user setting: DKC1_FRAMEGEN or View menu */
static int s_framegen_supported;  /* display refresh is an even multiple of 60 */
static int s_framegen_force;      /* DKC1_FRAMEGEN=force: present regardless */
static int s_framegen_last_valid;
static Dkc1FrameGenStats s_framegen_stats;
static double s_present_fps_value;
static int s_present_frames_window;
static long s_framegen_dump_start = -1;
static long s_framegen_dump_count;

static SRWLOCK s_present_lock = SRWLOCK_INIT;
static void CancelMidPresent(void);
static int s_presenter_d3d;                 /* 1: Direct3D 11 flip model */
static int s_window_minimized;             /* WM_SIZE SIZE_MINIMIZED */
static const char *s_dpi_awareness_mode = "unaware";
static const char *s_priority_mode = "normal";

#include "win32_present.inc"

static int FrameGenActive(void) {
  return s_framegen_enabled;
}
static int FrameGenExtraRefresh(void) {
  return s_framegen_enabled && (s_framegen_supported || s_framegen_force);
}

static void ApplyFrameGenSetting(void) {
  CancelMidPresent();
  s_smooth_valid = 0;
  Dkc1FrameGenSetEnabled(FrameGenActive() != 0);
  if (!FrameGenActive()) {
    s_framegen_last_valid = 0;
    memset(&s_framegen_stats, 0, sizeof s_framegen_stats);
  }
}

static void ForgetFrameGenHistory(void) {
  CancelMidPresent();
  Dkc1FrameGenInvalidate();
  s_prev_pixels_valid = 0;
  s_framegen_last_valid = 0;
  s_smooth_valid = 0;
}

static void WritePpm(const char *path, const uint8_t *pixels, int width,
                     int height) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int i = 0; i < width * height; i++) {
    const uint8_t rgb[3] = {pixels[i * 4 + 2], pixels[i * 4 + 1],
                            pixels[i * 4 + 0]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

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
  AppendMenuA(view, MF_STRING, kMenuFrameGen,
              "Smooth Animation / Frame &Generation\tF10");
  HMENU scaling = CreatePopupMenu();
  AppendMenuA(scaling, MF_STRING, kMenuScalingSharp, "&Sharp Bilinear");
  AppendMenuA(scaling, MF_STRING, kMenuScalingNearest, "&Nearest");
  AppendMenuA(scaling, MF_STRING, kMenuScalingLinear, "&Linear");
  HMENU pixel_aspect = CreatePopupMenu();
  AppendMenuA(pixel_aspect, MF_STRING, kMenuPixelAspectSnes,
              "SNES &7:6 pixels");
  AppendMenuA(pixel_aspect, MF_STRING, kMenuPixelAspectSquare,
              "S&quare pixels");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)aspect, "&Aspect Ratio");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)pixel_aspect, "Pi&xel Aspect");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)scaling, "S&caling");
  AppendMenuA(view, MF_POPUP, (UINT_PTR)layers, "&Layers");
  HMENU mods = CreatePopupMenu();
  AppendMenuA(mods, MF_STRING, kMenuToggleDixie,
              "&Dixie Kong Country");
  HMENU bar = CreateMenu();
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)emulation, "&Emulation");
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
  AppendMenuA(bar, MF_POPUP, (UINT_PTR)mods, "&Mods");
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
  CheckMenuItem(s_menu, kMenuFrameGen,
                MF_BYCOMMAND |
                    (s_framegen_enabled ? MF_CHECKED : MF_UNCHECKED));
  CheckMenuRadioItem(s_menu, kMenuAspectNative, kMenuAspectWidescreen,
                     Dkc1VideoIsWidescreen() ? kMenuAspectWidescreen
                                             : kMenuAspectNative,
                     MF_BYCOMMAND);
  CheckMenuItem(
      s_menu, kMenuToggleDixie,
      MF_BYCOMMAND | (Dkc1DixieIsVariant() || Dkc1DixieSavedEnabled()
                          ? MF_CHECKED
                          : MF_UNCHECKED));
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
  CheckMenuRadioItem(s_menu, kMenuScalingSharp, kMenuScalingLinear,
                     (UINT)(kMenuScalingSharp + s_scaling_mode),
                     MF_BYCOMMAND);
  CheckMenuRadioItem(s_menu, kMenuPixelAspectSnes, kMenuPixelAspectSquare,
                     s_square_pixels ? kMenuPixelAspectSquare
                                     : kMenuPixelAspectSnes,
                     MF_BYCOMMAND);
}

static void UpdateDebugTitle(void) {
  if (!s_window) return;
  char title[320];
  snprintf(title, sizeof title,
           "DKC1Recomp %s | frame %ld | %s | %s | %s | provenance %s | "
           "framegen %s",
           DKC1_BUILD_COMMIT, s_host_frame, s_paused ? "PAUSED" : "running",
           Dkc1VideoIsWidescreen() ? "16:9" : "4:3",
           LayerModeName(Dkc1DebugLayerMask()),
           Dkc1DebugProvenanceOverlay() ? "ON" : "off",
           FrameGenActive() ? "ON"
                            : s_framegen_enabled ? "unsupported" : "off");
  /* SetWindowText forces a non-client repaint; only send it when the
   * text changed (the frame counter advances the title once a second). */
  static char last_title[320];
  if (strcmp(title, last_title) != 0) {
    snprintf(last_title, sizeof last_title, "%s", title);
    SetWindowTextA(s_window, title);
  }
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
  if (s_inspect_armed_provenance) {
    s_inspect_armed_provenance = 0;
    WsShadowDebugSetProvenanceEnabled(false);
  }
  snprintf(s_host_status, sizeof s_host_status,
           "pixel (%d,%d) inspected", s_inspect_x, s_inspect_y);
}

/* Host-side FPS badge, composed into a DIB so nothing flickers; never
 * rendered into the framebuffer evidence. */
static HostSurface s_badge_surface;
static HostSurface s_panel_surface;
static int s_panel_surface_valid;

static int ComposeBadgeSurface(void) {
  const int width = HostDpiScale(kHostBadgeWidth);
  const int height = HostDpiScale(kHostBadgeHeight);
  if (!HostSurfaceEnsure(&s_badge_surface, width, height)) return 0;
  HDC dc = s_badge_surface.dc;
  RECT rect = {0, 0, width, height};
  FillRect(dc, &rect, MenubarBrush());
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(126, 217, 87));
  HFONT old_font = (HFONT)SelectObject(dc, HostPanelFont());
  char text[32];
  if (FrameGenActive())
    snprintf(text, sizeof text, "%3.0f/%3.0f FPS", s_fps_value,
             s_present_fps_value);
  else
    snprintf(text, sizeof text, "%5.1f FPS", s_fps_value);
  DrawTextA(dc, text, -1, &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old_font);
  return 1;
}

/* Compose the debug panel text into its DIB.  This runs on the producer
 * thread before the present call, never inside the compositor's sampling
 * window, and is skipped for half-frame presents. */
static void ComposePanelSurface(int width, int height) {
  if (!HostSurfaceEnsure(&s_panel_surface, width, height)) {
    s_panel_surface_valid = 0;
    return;
  }
  HDC panel_dc = s_panel_surface.dc;
  RECT panel = {0, 0, width, height};
  HBRUSH background = CreateSolidBrush(RGB(18, 21, 25));
  FillRect(panel_dc, &panel, background);
  DeleteObject(background);
  SetBkMode(panel_dc, TRANSPARENT);
  SetTextColor(panel_dc, RGB(222, 230, 238));
  HFONT old_font = (HFONT)SelectObject(panel_dc, HostPanelFont());

  char invariant_summary[160];
  char script[256] = "manual keyboard input";
  if (s_script_loaded) Dkc1ScriptStatus(script, sizeof script);
  else if (s_input_playback.count)
    snprintf(script, sizeof script, "input playback: %zu frames",
             s_input_playback.count);
  char framegen_line[128];
  if (FrameGenActive()) {
    snprintf(framegen_line, sizeof framegen_line,
             "%s; 67ms buffer; %u groups / %u pixels",
             FrameGenExtraRefresh() ? "60/120 Hz" : "60 Hz",
             s_framegen_stats.pose_actors, s_framegen_stats.pose_pixels);
  } else {
    snprintf(framegen_line, sizeof framegen_line, "off (F10)");
  }
  char text[2048];
  snprintf(text, sizeof text,
           "VISIBLE WIDESCREEN DEBUGGER\r\n"
           "Build: %s\r\n"
           "Presenter: %s  DPI %d (%s)\r\n"
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
           "Smooth animation: %s\r\n"
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
           "F10 smooth animation / frame generation\r\n"
           "F11 quick save   F12 quick load\r\n"
           "Alt+Enter fullscreen; Esc returns\r\n"
           "Esc quit (when windowed)\r\n"
           "\r\n"
           "The side panel is host-only and is not\r\n"
           "included in framebuffer evidence.",
           s_build_id,
           s_presenter_d3d ? "Direct3D 11 flip" : "GDI", s_dpi,
           s_dpi_awareness_mode,
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
           framegen_line,
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
  text_rect.left += HostDpiScale(12);
  text_rect.top += HostDpiScale(12);
  text_rect.right -= HostDpiScale(10);
  DrawTextA(panel_dc, text, -1, &text_rect,
            DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
  SelectObject(panel_dc, old_font);
  s_panel_surface_valid = 1;
}

/* Present one complete host framebuffer through the active presenter.
 * `pixels` is either the real frame or the host-only in-between frame;
 * the panel is recomposed only when asked so a half-period present does
 * not pay for text layout twice.  `dc` is only used by the GDI path (a
 * WM_PAINT device context); NULL acquires one.  Callers hold
 * s_present_lock. */
static void PresentPixelsUnlocked(HDC dc, const uint8_t *pixels,
                                  int compose_panel, UINT sync_interval) {
  if (!s_window || !s_width || !s_height || !pixels) return;
  if (s_window_minimized) return;  /* nothing to show; the loop keeps time */
  RECT client;
  GetClientRect(s_window, &client);
  const int cw = client.right, ch = client.bottom;
  if (cw <= 0 || ch <= 0) return;
  const RECT dest = HostGameRect(cw, ch);
  const int dw = dest.right - dest.left, dh = dest.bottom - dest.top;
  const int show_panel = s_panel_enabled && !s_fullscreen;
  const int panel_width = HostPanelWidth();
  if (show_panel && compose_panel) ComposePanelSurface(panel_width, ch);
  const int badge_x = dest.left + HostDpiScale(8);
  const int badge_y = dest.top + HostDpiScale(8);

  if (s_presenter_d3d && s_d3d.active) {
    static const float kDark[4] = {18.0f / 255.0f, 21.0f / 255.0f,
                                   25.0f / 255.0f, 1.0f};
    static const float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int bw, bh;
    if (HostD3DBeginFrame(&bw, &bh, s_fullscreen ? kBlack : kDark)) {
      HostD3DUpload(&s_d3d.frame, pixels, s_width, s_height,
                    (size_t)s_width * 4);
      HostD3DDrawQuad(&s_d3d.frame, &dest, s_scaling_mode);
      if (show_panel && s_panel_surface_valid) {
        if (compose_panel)
          HostD3DUpload(&s_d3d.panel, s_panel_surface.pixels,
                        s_panel_surface.width, s_panel_surface.height,
                        (size_t)s_panel_surface.width * 4);
        RECT panel_rect = {dest.right, 0, dest.right + panel_width, ch};
        HostD3DDrawQuad(&s_d3d.panel, &panel_rect, kHostScalingNearest);
      }
      if (s_show_fps && ComposeBadgeSurface() &&
          HostD3DUpload(&s_d3d.badge, s_badge_surface.pixels,
                        s_badge_surface.width, s_badge_surface.height,
                        (size_t)s_badge_surface.width * 4)) {
        RECT badge_rect = {badge_x, badge_y,
                           badge_x + s_badge_surface.width,
                           badge_y + s_badge_surface.height};
        HostD3DDrawQuad(&s_d3d.badge, &badge_rect, kHostScalingNearest);
      }
      HostD3DPresent(sync_interval);
      return;
    }
    /* Device lost or resize failure: the loop notices s_d3d.active == 0
     * and moves to GDI; draw this frame with GDI right away. */
  }

  HDC owned = NULL;
  if (!dc) {
    owned = GetDC(s_window);
    dc = owned;
  }
  if (!dc || !s_bmi.bmiHeader.biSize) return;
  if (s_fullscreen) {
    /* Aspect-preserving letterbox across the whole monitor; bars are
     * repainted with the same black every frame, so nothing flickers. */
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bar;
    if (dest.top > 0) {
      SetRect(&bar, 0, 0, cw, dest.top);
      FillRect(dc, &bar, black);
      SetRect(&bar, 0, dest.bottom, cw, ch);
      FillRect(dc, &bar, black);
    }
    if (dest.left > 0) {
      SetRect(&bar, 0, 0, dest.left, ch);
      FillRect(dc, &bar, black);
      SetRect(&bar, dest.right, 0, cw, ch);
      FillRect(dc, &bar, black);
    }
  }
  if (s_scaling_mode == kHostScalingLinear) {
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
  } else {
    SetStretchBltMode(dc, COLORONCOLOR);  /* crisp pixels, no smoothing */
  }
  StretchDIBits(dc, dest.left, dest.top, dw, dh, 0, 0, s_width, s_height,
                pixels, &s_bmi, DIB_RGB_COLORS, SRCCOPY);
  if (s_show_fps && ComposeBadgeSurface())
    BitBlt(dc, badge_x, badge_y, s_badge_surface.width,
           s_badge_surface.height, s_badge_surface.dc, 0, 0, SRCCOPY);
  if (show_panel && s_panel_surface_valid)
    BitBlt(dc, dest.right, 0, panel_width, ch, s_panel_surface.dc, 0, 0,
           SRCCOPY);
  GdiFlush();
  if (owned) ReleaseDC(s_window, owned);
}

static void PresentFrame(const uint8_t *pixels, int compose_panel,
                         UINT sync_interval) {
  AcquireSRWLockExclusive(&s_present_lock);
  PresentPixelsUnlocked(NULL, pixels, compose_panel, sync_interval);
  ReleaseSRWLockExclusive(&s_present_lock);
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
static double s_audio_mix_ms, s_audio_submit_ms;

static double AudioTraceTick(void) {
  LARGE_INTEGER tick, frequency;
  if (!s_audio_log_stats) return 0.0;
  QueryPerformanceCounter(&tick);
  QueryPerformanceFrequency(&frequency);
  return (double)tick.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

/* Resize the windowed frame to fit the game view plus the optional panel. */
static void AdjustWindowRectForHostDpi(RECT *rect) {
  HMODULE user32 = GetModuleHandleA("user32.dll");
  typedef BOOL (WINAPI *AdjustFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
  AdjustFn adjust = user32
      ? (AdjustFn)GetProcAddress(user32, "AdjustWindowRectExForDpi") : NULL;
  if (!adjust || !adjust(rect, kWindowedStyle, TRUE, 0, (UINT)s_dpi))
    AdjustWindowRect(rect, kWindowedStyle, TRUE);
}

static void ApplyWindowedSize(void) {
  if (!s_window || s_fullscreen) return;
  int width, height;
  HostWindowedClientSize(&width, &height);
  RECT rect = {0, 0, width, height};
  AdjustWindowRectForHostDpi(&rect);
  SetWindowPos(s_window, NULL, 0, 0, rect.right - rect.left,
               rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  HostD3DRequestResize();
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
  ForgetFrameGenHistory();  /* buffers changed width */
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
  CancelMidPresent();
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
  HostD3DRequestResize();
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
    case WM_SIZE:
      CancelMidPresent();
      s_window_minimized = wp == SIZE_MINIMIZED;
      if (wp != SIZE_MINIMIZED) HostD3DRequestResize();
      break;
    case WM_ENTERMENULOOP:
      CancelMidPresent();
      break;
    case WM_ERASEBKGND:
      /* The swap chain covers the client area; a GDI erase would flash. */
      if (s_presenter_d3d && s_d3d.active) return 1;
      break;
    case WM_DPICHANGED: {
      /* Per-monitor DPI: keep an integer window scale and let the
       * suggested rectangle place the window on the new monitor. */
      const int dpi = (int)HIWORD(wp);
      const RECT *suggested = (const RECT *)lp;
      if (dpi >= 48 && dpi <= 960) {
        s_dpi = dpi;
        s_window_scale = HostWindowScaleForDpi(s_dpi);
      }
      if (!s_fullscreen && suggested) {
        int width, height;
        HostWindowedClientSize(&width, &height);
        RECT rect = {0, 0, width, height};
        AdjustWindowRectForHostDpi(&rect);
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      HostD3DRequestResize();
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    }
    case WM_CLOSE:
    case WM_DESTROY:
      CancelMidPresent();
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
      HostClientToGame(hwnd, click_x, click_y, &game_x, &game_y);
      if (game_x >= 0) {
        s_inspect_x = game_x;
        s_inspect_y = game_y;
        s_inspect_pending = 2; /* resolve after a provenance-armed render */
        /* Arm provenance for this inspection only; ResolvePixelInspect
         * disarms it unless the overlay was already on.  Left armed, it
         * bypassed smoothing and the 120 Hz midpoints for the rest of the
         * session (the render skips provenance-armed frames). */
        if (!WsShadowDebugProvenanceEnabled()) {
          WsShadowDebugSetProvenanceEnabled(true);
          s_inspect_armed_provenance = 1;
        }
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
      if (wp == VK_F10) {  /* F10 arrives as a system key */
        if (!(lp & (1u << 30))) {
          s_framegen_enabled = !s_framegen_enabled;
          ApplyFrameGenSetting();
          snprintf(s_host_status, sizeof s_host_status,
                   "frame generation %s%s",
                   s_framegen_enabled ? "enabled" : "disabled",
                   s_framegen_enabled ? " (67 ms display buffer)" : "");
          UpdateDebugTitle();
        }
        return 0;
      }
      break;
    case WM_SYSKEYUP:
      if (wp == VK_F10) return 0;  /* keep F10 from entering menu mode */
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
        case kMenuToggleDixie:
          if (Dkc1DixieIsVariant()) {
            /* The variant's menu item switches back to stock. */
            Dkc1DixieSwitchAndRelaunch(0, "dkc1_dixie_desktop.exe",
                                       "dkc1_desktop.exe", s_host_status,
                                       sizeof s_host_status);
          } else if (!Dkc1DixieSavedEnabled()) {
            /* No ROM picker needed: the variant synthesizes the mod from
             * the same clean-ROM argument. */
            Dkc1DixieSwitchAndRelaunch(1, "dkc1_dixie_desktop.exe",
                                       "dkc1_desktop.exe", s_host_status,
                                       sizeof s_host_status);
          } else {
            Dkc1DixieSetEnabled(0);
            snprintf(s_host_status, sizeof s_host_status,
                     "Dixie Kong Country will stay off from now on");
          }
          RefreshMenuChecks();
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
        case kMenuFrameGen:
          s_framegen_enabled = !s_framegen_enabled;
          ApplyFrameGenSetting();
          snprintf(s_host_status, sizeof s_host_status,
                   "frame generation %s%s",
                   s_framegen_enabled ? "enabled" : "disabled",
                   s_framegen_enabled ? " (67 ms display buffer)" : "");
          break;
        case kMenuScalingSharp:
          s_scaling_mode = kHostScalingSharp;
          break;
        case kMenuScalingNearest:
          s_scaling_mode = kHostScalingNearest;
          break;
        case kMenuScalingLinear:
          s_scaling_mode = kHostScalingLinear;
          break;
        case kMenuPixelAspectSnes:
          s_square_pixels = 0;
          ApplyWindowedSize();
          break;
        case kMenuPixelAspectSquare:
          s_square_pixels = 1;
          ApplyWindowedSize();
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
      /* The flip-model presenter redraws on its own cadence (the paused
       * loop keeps presenting), so only the GDI path paints here. */
      if (!(s_presenter_d3d && s_d3d.active)) {
        AcquireSRWLockExclusive(&s_present_lock);
        PresentPixelsUnlocked(dc, s_smooth_valid ? s_smooth_pixels : s_pixels,
                              1, 0);
        ReleaseSRWLockExclusive(&s_present_lock);
      }
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
  s_audio_mix_ms = s_audio_submit_ms = 0.0;
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
  const double mix_start = AudioTraceTick();
  memset(samples, 0, (size_t)frames * 4);
  RtlRenderAudio(samples, frames, 2);
  s_audio_mix_ms = AudioTraceTick() - mix_start;
  if (s_audio_log_stats) {
    audio_trace_get_stats(&audio_stats);
    s_audio_ring_frames = audio_stats.occupancy_current;
    s_audio_internal_underflows = audio_stats.output_underflows;
  }
  header->dwBufferLength = (DWORD)frames * 4;
  const double submit_start = AudioTraceTick();
  if (waveOutWrite(s_waveout, header, sizeof *header) != MMSYSERR_NOERROR) {
    s_audio_submit_ms = AudioTraceTick() - submit_start;
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
  s_audio_submit_ms = AudioTraceTick() - submit_start;
}

/* Frame pacer.
 *
 * Three ways to find the next presentation slot, chosen once at start-up:
 *
 *  - waitable (Direct3D 11 presenter, display refresh an integer multiple
 *    of ~60 Hz): block on the swap chain's frame-latency waitable object.
 *    It is signalled when the previous image has been consumed by the
 *    compositor or flipped to the screen, so the cadence is the display's
 *    own and there is no period estimate to drift, no lead to tune and no
 *    vblank timestamp to interpret.  Present(divisor) shows each emulated
 *    frame for exactly `divisor` refreshes.
 *  - dwmflush (GDI fallback on the same displays): block on the
 *    compositor's frame boundary with DwmFlush, then blit immediately.
 *    The GDI update lands at the very start of the compositor's window
 *    instead of racing its sampling point.
 *  - timer (DKC1_PRESENT_HZ override, incompatible refresh, or
 *    DKC1_FRAMEGEN=force): the previous absolute QPC schedule.  A missed
 *    deadline re-anchors to "now" and is never followed by a catch-up
 *    frame.  The D3D presenter uses sync interval 0 here so the latest
 *    queued image is shown and the timer alone sets the cadence.
 *
 * In the compositor-locked modes the loop waits first, then polls input,
 * emulates, renders and presents, which removes most of a frame of input
 * latency compared with the old work-then-hold ordering.  Timer mode and
 * the 120 Hz frame-generation path keep the deterministic work-first
 * ordering because their submit time, not the compositor, sets cadence. */
typedef enum HostPaceMode {
  kHostPaceTimer = 0,
  kHostPaceWaitable,
  kHostPaceDwmFlush,
} HostPaceMode;

typedef HRESULT (WINAPI *HostDwmTimingFn)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *HostDwmFlushFn)(void);

typedef struct HostFramePacer {
  LARGE_INTEGER frequency;
  HANDLE timer;
  HMODULE dwm_module;
  HostDwmTimingFn dwm_timing;
  HostDwmFlushFn dwm_flush;
  HostPaceMode mode;
  int locked;                 /* display refresh is a multiple of ~60 Hz */
  int divisor;                /* display refreshes per emulated frame */
  int mid_flushes;            /* worker copy: compositor passes per half */
  UINT mid_sync_interval;     /* worker copy */
  double refresh_hz;          /* emulated frames per second */
  double display_hz;
  double period_ticks;
  double half_period_ticks;
  double next_present_tick;   /* timer mode schedule */
  double wake_tick;           /* when the last frame wait returned */
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
  int pending_wait_timeout;
  double pending_pump_ms;     /* message pump this iteration */
  double pending_slow_msg_ms; /* slowest dispatched message */
  UINT pending_slow_msg;
  double pending_stats_ms;    /* DXGI statistics query */
  double pending_gap_ms;      /* previous present completion -> this wait */
  double last_presented_tick;
  double last_wake_tick;
  double pending_wake_interval_ms; /* slot cadence: wait return to return */
  unsigned long long pending_interp_steps; /* interpreted opcodes this frame */
  long pending_tier_hits;                  /* dispatch tier-downs this frame */
  unsigned pending_interp_bank;            /* bank with most interpreted ops */
  unsigned long long pending_interp_bank_steps;
  unsigned long long interp_bank_before[256];
  int minimized_pacing;       /* waitable mode parked on the timer */
  unsigned long overruns;
  unsigned long frames;
  long test_stall_frame;
  DWORD test_stall_ms;
  int test_stall_fired;
  int timer_resolution_active;
  const char *clock_source;
  FILE *log;
  HostPacingLog *async_log;
  double pending_log_ms;
  double wait_start_tick;
  double loop_start_tick;
  unsigned long mid_skips;
  int pending_mid_presented;
  double pending_interp_ms;
  double pending_mid_submit_tick;
  double pending_mid_submit_error_ms;
  double pending_mid_present_ms;
  double pending_real_to_mid_ms;
  long pending_mid_after_frame;
  HostScanoutStats scanout;
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

static int HostFramePacerReadDwm(HostFramePacer *pacer,
                                 DWM_TIMING_INFO *timing) {
  if (!pacer->dwm_timing) return 0;
  memset(timing, 0, sizeof *timing);
  timing->cbSize = sizeof *timing;
  /* The desktop-wide query exposes the composition clock even before this
   * window has accumulated per-window present statistics. */
  return SUCCEEDED(pacer->dwm_timing(NULL, timing)) &&
         timing->qpcRefreshPeriod > 0;
}

/* Display refresh in Hz: the compositor's rational rate, else the device
 * mode, else 0.  Only used to pick the integer divisor and the nominal
 * audio request rate; the compositor-locked modes never schedule from it. */
static double HostFramePacerDisplayHz(HostFramePacer *pacer) {
  DWM_TIMING_INFO timing;
  if (HostFramePacerReadDwm(pacer, &timing)) {
    pacer->clock_source = "dwm";
    if (timing.rateRefresh.uiDenominator > 0 &&
        timing.rateRefresh.uiNumerator > 0)
      return (double)timing.rateRefresh.uiNumerator /
             (double)timing.rateRefresh.uiDenominator;
    return (double)pacer->frequency.QuadPart /
           (double)timing.qpcRefreshPeriod;
  }
  HDC dc = GetDC(s_window);
  const int display_hz = dc ? GetDeviceCaps(dc, VREFRESH) : 0;
  if (dc) ReleaseDC(s_window, dc);
  if (display_hz > 1) {
    pacer->clock_source = "display";
    return (double)display_hz;
  }
  return 0.0;
}

static const char *HostPaceModeName(HostPaceMode mode) {
  switch (mode) {
    case kHostPaceWaitable: return "waitable";
    case kHostPaceDwmFlush: return "dwmflush";
    default: return "timer";
  }
}

static const char *HostScalingName(void) {
  switch (s_scaling_mode) {
    case kHostScalingNearest: return "nearest";
    case kHostScalingLinear: return "linear";
    default: return "sharp";
  }
}

static int HostFramePacerInit(HostFramePacer *pacer) {
  LARGE_INTEGER now;
  memset(pacer, 0, sizeof *pacer);
  if (!QueryPerformanceFrequency(&pacer->frequency) ||
      !QueryPerformanceCounter(&now))
    return 0;
  pacer->clock_source = "hardware";
  pacer->dwm_module = LoadLibraryA("dwmapi.dll");
  if (pacer->dwm_module) {
    pacer->dwm_timing = (HostDwmTimingFn)GetProcAddress(
        pacer->dwm_module, "DwmGetCompositionTimingInfo");
    pacer->dwm_flush =
        (HostDwmFlushFn)GetProcAddress(pacer->dwm_module, "DwmFlush");
  }
  if (HostFramePacerOverrideHz(&pacer->refresh_hz)) {
    pacer->clock_source = "override";
  } else {
    pacer->display_hz = HostFramePacerDisplayHz(pacer);
    if (pacer->display_hz > 0.0) {
      const int divisor = (int)(pacer->display_hz / 60.0 + 0.5);
      const double divided_hz =
          divisor > 0 ? pacer->display_hz / (double)divisor : 0.0;
      /* An exact SNES cadence drifts through a 60 Hz compositor and
       * produces a periodic doubled/dropped presentation.  Lock to the
       * display when an integer divisor lands within 59.5-60.5 Hz. */
      if (divided_hz >= 59.5 && divided_hz <= 60.5) {
        pacer->locked = 1;
        pacer->divisor = divisor;
        pacer->refresh_hz = divided_hz;
      }
    }
    if (!pacer->locked) {
      pacer->refresh_hz = 60.098811862;  /* hardware cadence */
      pacer->clock_source = "hardware";
    }
  }
  if (pacer->locked) {
    if (s_presenter_d3d && s_d3d.active) pacer->mode = kHostPaceWaitable;
    else if (pacer->dwm_flush) pacer->mode = kHostPaceDwmFlush;
    else pacer->mode = kHostPaceTimer;
  } else {
    pacer->mode = kHostPaceTimer;
  }
  /* DKC1_FRAMEGEN=force wants the 120 Hz submission path on a display
   * that cannot show it; only the free-running timer can submit twice
   * per emulated frame without stalling on the compositor. */
  if (s_framegen_enabled && s_framegen_force &&
      !(pacer->locked && pacer->divisor >= 2 && (pacer->divisor % 2) == 0))
    pacer->mode = kHostPaceTimer;
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
  pacer->period_ticks =
      (double)pacer->frequency.QuadPart / pacer->refresh_hz;
  /* Frame generation needs the in-between image to land on its own display
   * refresh: an even divisor gives it exactly half the emulated period. */
  s_framegen_supported =
      pacer->locked && pacer->divisor >= 2 && (pacer->divisor % 2) == 0;
  pacer->half_period_ticks = pacer->period_ticks * 0.5;
  pacer->next_present_tick = (double)now.QuadPart;
  pacer->wake_tick = (double)now.QuadPart;
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
        pacer->async_log = HostPacingLogOpen(pacer->log, path);
        if (!pacer->async_log) {
          fclose(pacer->log);
          pacer->log = NULL;
          fprintf(stderr, "[pacing] unable to create diagnostic queue\n");
          return 1;
        }
        s_audio_log_stats = 1;
        HostPacingLogWrite(pacer->async_log,
                "{\"schema\":\"dkc1.pacing.v5\",\"refresh_hz\":%.9f,"
                "\"display_hz\":%.9f,\"clock_source\":\"%s\","
                "\"pacing\":\"%s\",\"presenter\":\"%s\","
                "\"presenter_error\":\"%.120s\","
                "\"dpi\":%d,\"dpi_awareness\":\"%s\",\"window_scale\":%d,"
                "\"scaling\":\"%s\",\"pixel_aspect\":\"%s\","
                "\"thread_priority\":\"%s\","
                "\"submit_lead_ms\":0.0000,\"audio_preroll\":%d,"
                "\"audio_ring_start_frames\":%d,"
                "\"present_divisor\":%d,\"framegen\":%d,"
                "\"framegen_extra_refresh\":%d,"
                "\"framegen_half_period_ms\":%.4f,"
                "\"test_stall_frame\":%ld,\"test_stall_ms\":%lu,"
                "\"log_mode\":\"async\",\"log_capacity\":%u,"
                "\"max_frame_latency\":%u,\"pid\":%lu,\"main_tid\":%lu,"
                "\"qpc_frequency\":%lld}\n",
                pacer->refresh_hz, pacer->display_hz,
                pacer->clock_source, HostPaceModeName(pacer->mode),
                s_presenter_d3d ? "d3d11" : "gdi", s_d3d.error,
                s_dpi, s_dpi_awareness_mode, s_window_scale,
                HostScalingName(), s_square_pixels ? "1:1" : "7:6",
                s_priority_mode,
                s_audio_preroll_buffers, kAudioRingStartFrames,
                pacer->divisor, FrameGenActive(), FrameGenExtraRefresh(),
                pacer->half_period_ticks * 1000.0 /
                    (double)pacer->frequency.QuadPart,
                pacer->test_stall_frame,
                (unsigned long)pacer->test_stall_ms, kPacingLogSlots,
                s_d3d.max_frame_latency, GetCurrentProcessId(), GetCurrentThreadId(),
                pacer->frequency.QuadPart);
      }
    }
  }
  return 1;
}

/* Sync interval for a Present call: exactly `divisor` refreshes per
 * emulated frame when compositor-locked; 0 in timer mode so the timer alone
 * sets cadence and the newest queued image wins.  `paired` marks both
 * images of a 120 Hz frame-generation pair, which share the divisor half
 * and half.  A real frame that no midpoint follows must keep the whole
 * divisor: the swap chain releases the next slot as soon as its image has
 * been consumed, so a half-divisor present with no partner would advance
 * emulation every refresh (double speed on a 120 Hz display). */
static UINT HostFramePacerSyncInterval(const HostFramePacer *pacer,
                                       int paired) {
  if (pacer->mode != kHostPaceWaitable) return 0;
  return (UINT)HostPresentRefreshes(pacer->divisor,
                                    paired && FrameGenExtraRefresh());
}

static void HostFramePacerInjectTestStall(HostFramePacer *pacer,
                                           long host_frame) {
  if (!pacer->test_stall_fired && pacer->test_stall_ms &&
      host_frame == pacer->test_stall_frame) {
    pacer->test_stall_fired = 1;
    Sleep(pacer->test_stall_ms);
  }
}

/* Pause/resume: drop any half-frame and restart the timer schedule from
 * now.  The compositor-locked modes have no schedule to restart. */
static void HostFramePacerReset(HostFramePacer *pacer) {
  CancelMidPresent();
  pacer->pending_mid_presented = 0;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  pacer->next_present_tick = (double)now.QuadPart;
  pacer->last_submit_tick = 0.0;
  pacer->last_present_tick = 0.0;
}

/* Block until the absolute tick `target`: a high-resolution timer for the
 * coarse wait, then a bounded spin for the last millisecond. */
static void HostFramePacerWaitUntil(HostFramePacer *pacer, double target) {
  LARGE_INTEGER now;
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
}

#include "win32_mid_present.inc"

/* Block until the next emulated-frame slot.  `work_first` callers have
 * already done this frame's work (timer mode, 120 Hz frame generation);
 * their work time is measured here.  Wait-first callers measure it at
 * present time. */
static void HostFramePacerWaitFrame(HostFramePacer *pacer,
                                    double work_start_tick, int work_first) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double before_wait = (double)now.QuadPart;
  pacer->wait_start_tick = before_wait;
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  pacer->pending_wait_timeout = 0;
  pacer->pending_late_ms = 0.0;
  if (work_first)
    pacer->pending_work_ms = (before_wait - work_start_tick) / ticks_per_ms;
  pacer->pending_gap_ms = pacer->last_presented_tick > 0.0
      ? (before_wait - pacer->last_presented_tick) / ticks_per_ms -
            (work_first ? pacer->pending_work_ms : 0.0)
      : 0.0;
  HostPaceMode mode = pacer->mode;
  if (mode == kHostPaceWaitable && s_window_minimized) {
    /* A minimized flip-model window stops consuming presents for a while
     * before DXGI throttles it; park on the timer instead of timing out
     * (nothing is presented while minimized). */
    if (!pacer->minimized_pacing) {
      pacer->minimized_pacing = 1;
      pacer->next_present_tick = before_wait;
    }
    mode = kHostPaceTimer;
  } else if (pacer->minimized_pacing) {
    pacer->minimized_pacing = 0;
  }
  switch (mode) {
    case kHostPaceWaitable:
      if (!HostD3DWaitForSlot(250)) {
        pacer->pending_wait_timeout = 1;
        pacer->overruns++;
      }
      break;
    case kHostPaceDwmFlush: {
      /* The worker consumed half the divisor's passes when it presented
       * the midpoint; a frame without one waits for all of them. */
      const int passes = HostProducerFlushPasses(
          pacer->divisor,
          FrameGenExtraRefresh() && pacer->pending_mid_presented);
      for (int i = 0; i < passes; i++) {
        if (!pacer->dwm_flush || FAILED(pacer->dwm_flush())) {
          pacer->pending_wait_timeout = 1;
          pacer->overruns++;
          break;
        }
      }
      break;
    }
    default: {
      double target = pacer->next_present_tick + pacer->period_ticks;
      if (before_wait > target) {
        /* Missed deadline: give the next frame a complete interval from
         * now rather than chasing the schedule with a short frame. */
        pacer->pending_late_ms = (before_wait - target) / ticks_per_ms;
        pacer->overruns++;
        target = before_wait;
      }
      pacer->next_present_tick = target;
      HostFramePacerWaitUntil(pacer, target);
      break;
    }
  }
  QueryPerformanceCounter(&now);
  pacer->wake_tick = (double)now.QuadPart;
  pacer->pending_wait_ms = (pacer->wake_tick - before_wait) / ticks_per_ms;
  /* In wait-first modes the present-call spacing absorbs work variance;
   * the slot cadence is the spacing of the wait returns. */
  pacer->pending_wake_interval_ms = pacer->last_wake_tick > 0.0
      ? (pacer->wake_tick - pacer->last_wake_tick) / ticks_per_ms : 0.0;
  pacer->last_wake_tick = pacer->wake_tick;
}

static void HostFramePacerBeginPresent(HostFramePacer *pacer,
                                       int work_first) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  pacer->pending_submit_tick = (double)now.QuadPart;
  pacer->pending_submit_interval_ms = pacer->last_submit_tick > 0.0
      ? (pacer->pending_submit_tick - pacer->last_submit_tick) / ticks_per_ms
      : 0.0;
  if (pacer->mode == kHostPaceTimer) {
    pacer->pending_submit_error_ms =
        (pacer->pending_submit_tick - pacer->next_present_tick) /
        ticks_per_ms;
  } else {
    pacer->pending_submit_error_ms = 0.0;
  }
  if (!work_first) {
    /* Wait-first: the frame's budget runs from the slot signal to this
     * present; exceeding one period means the image misses its refresh. */
    pacer->pending_work_ms =
        (pacer->pending_submit_tick - pacer->wake_tick) / ticks_per_ms;
    const double budget_ms = pacer->period_ticks / ticks_per_ms;
    if (pacer->pending_work_ms > budget_ms) {
      pacer->pending_late_ms = pacer->pending_work_ms - budget_ms;
      pacer->overruns++;
    }
  }
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
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  const double interval_ms = pacer->last_present_tick > 0.0
      ? (present_tick - pacer->last_present_tick) / ticks_per_ms : 0.0;
  const double present_ms =
      (present_tick - pacer->pending_submit_tick) / ticks_per_ms;
  pacer->last_present_tick = present_tick;
  pacer->frames++;
  if (pacer->log && s_presenter_d3d) HostD3DScanout(&pacer->scanout);
  else memset(&pacer->scanout, 0, sizeof pacer->scanout);
  QueryPerformanceCounter(&now);
  pacer->pending_stats_ms = ((double)now.QuadPart - present_tick) / ticks_per_ms;
  if (pacer->log) {
    const HostScanoutStats *scan = &pacer->scanout;
    HostPacingLogWrite(pacer->async_log,
            "{\"frame\":%ld,\"work_ms\":%.4f,\"wait_ms\":%.4f,"
            "\"late_ms\":%.4f,\"present_interval_ms\":%.4f,"
            "\"submit_interval_ms\":%.4f,\"submit_error_ms\":%.4f,"
            "\"present_ms\":%.4f,\"setup_ms\":%.4f,"
            "\"emulation_ms\":%.4f,\"render_ms\":%.4f,"
            "\"diagnostics_ms\":%.4f,\"audio_ms\":%.4f,"
            "\"audio_queued_frames\":%d,\"audio_starvations\":%lu,"
            "\"audio_drops\":%lu,\"audio_ring_frames\":%lu,"
            "\"audio_internal_underflows\":%llu,"
            "\"framegen\":%d,\"interp_ms\":%.4f,\"interp_valid\":%d,"
            "\"interp_reject\":\"%s\",\"sprites_exact\":%u,"
            "\"sprites_actor\":%u,\"sprites_nearest\":%u,"
            "\"sprites_unmatched\":%u,\"actors_tracked\":%u,"
            "\"poses_changed\":%u,\"sprites_tween\":%u,\"tween_rects\":%u,"
            "\"tween_cells\":%u,\"tween_moved\":%u,\"tween_pixels\":%u,"
            "\"pose_actors\":%u,\"pose_pixels\":%u,\"pose_mismatch\":%u,"
            "\"pose_source_frame\":%d,"
            "\"max_scroll_step\":%d,\"mid_presented\":%d,"
            "\"mid_skips\":%lu,\"mid_submit_error_ms\":%.4f,"
            "\"mid_present_ms\":%.4f,\"mid_after_frame\":%ld,"
            "\"real_to_mid_ms\":%.4f,\"mid_to_real_ms\":%.4f,"
            "\"wait_timeout\":%d,\"present_count\":%u,"
            "\"stat_valid\":%d,\"stat_present_count\":%u,"
            "\"stat_present_refresh\":%u,\"stat_sync_refresh\":%u,"
            "\"stat_sync_qpc_ms\":%.4f,\"stat_disjoint\":%d,"
            "\"stat_lag_presents\":%d,\"pump_ms\":%.4f,\"slow_msg\":%u,"
            "\"slow_msg_ms\":%.4f,\"stats_ms\":%.4f,\"gap_ms\":%.4f,"
            "\"wake_interval_ms\":%.4f,\"interp_steps\":%llu,"
            "\"tier_hits\":%ld,\"interp_bank\":%u,"
            "\"interp_bank_steps\":%llu,\"overruns\":%lu,"
            "\"submit_qpc_ms\":%.4f,\"present_end_qpc_ms\":%.4f,"
            "\"wait_start_qpc_ms\":%.4f,\"wake_qpc_ms\":%.4f,"
            "\"loop_start_qpc_ms\":%.4f,\"previous_log_ms\":%.4f,"
            "\"log_dropped\":%u,\"audio_mix_ms\":%.4f,"
            "\"audio_submit_ms\":%.4f}\n",
            host_frame, pacer->pending_work_ms, pacer->pending_wait_ms,
            pacer->pending_late_ms, interval_ms,
            pacer->pending_submit_interval_ms,
            pacer->pending_submit_error_ms, present_ms,
            pacer->pending_setup_ms, pacer->pending_emulation_ms,
            pacer->pending_render_ms, pacer->pending_diagnostics_ms,
            pacer->pending_audio_ms, s_audio_last_queued_frames,
            s_audio_starvations, s_audio_drops, s_audio_ring_frames,
            s_audio_internal_underflows,
            FrameGenActive(), pacer->pending_interp_ms,
            s_framegen_last_valid,
            s_framegen_stats.reject ? s_framegen_stats.reject : "",
            s_framegen_stats.sprites_exact, s_framegen_stats.sprites_actor,
            s_framegen_stats.sprites_nearest,
            s_framegen_stats.sprites_unmatched,
            s_framegen_stats.actors_tracked,
            s_framegen_stats.poses_changed,
            s_framegen_stats.sprites_tween, s_framegen_stats.tween_rects,
            s_framegen_stats.tween_cells, s_framegen_stats.tween_moved,
            s_framegen_stats.tween_pixels,
            s_framegen_stats.pose_actors, s_framegen_stats.pose_pixels,
            s_framegen_stats.pose_mismatch,
            s_framegen_stats.pose_source_frame,
            s_framegen_stats.max_scroll_step,
            pacer->pending_mid_presented, pacer->mid_skips,
            pacer->pending_mid_submit_error_ms,
            pacer->pending_mid_present_ms, pacer->pending_mid_after_frame,
            pacer->pending_real_to_mid_ms,
            pacer->pending_mid_presented
                ? (pacer->pending_submit_tick -
                   pacer->pending_mid_submit_tick) / ticks_per_ms
                : 0.0,
            pacer->pending_wait_timeout, scan->present_count, scan->valid,
            scan->displayed_count, scan->displayed_refresh,
            scan->sync_refresh,
            (double)scan->sync_qpc / ticks_per_ms, scan->disjoint,
            scan->valid ? (int)(scan->present_count - scan->displayed_count)
                        : 0,
            pacer->pending_pump_ms, pacer->pending_slow_msg,
            pacer->pending_slow_msg_ms, pacer->pending_stats_ms,
            pacer->pending_gap_ms, pacer->pending_wake_interval_ms,
            pacer->pending_interp_steps, pacer->pending_tier_hits,
            pacer->pending_interp_bank, pacer->pending_interp_bank_steps,
            pacer->overruns, pacer->pending_submit_tick / ticks_per_ms,
            present_tick / ticks_per_ms, pacer->wait_start_tick / ticks_per_ms,
            pacer->wake_tick / ticks_per_ms, pacer->loop_start_tick / ticks_per_ms,
            pacer->pending_log_ms, pacer->async_log->dropped,
            s_audio_mix_ms, s_audio_submit_ms);
    pacer->pending_mid_submit_error_ms = 0.0;
    pacer->pending_mid_present_ms = 0.0;
    pacer->pending_mid_presented = 0;
    pacer->pending_interp_ms = 0.0;
  }
  QueryPerformanceCounter(&now);
  pacer->pending_log_ms = ((double)now.QuadPart - present_tick) / ticks_per_ms -
                          pacer->pending_stats_ms;
  /* Include statistics and logging in the next gap: no unmeasured hole. */
  pacer->last_presented_tick = present_tick;
}

/* The Direct3D device was lost mid-run: continue with GDI on the
 * compositor-flush clock (or the timer when DWM is unavailable). */
static void HostFramePacerFallBackToGdi(HostFramePacer *pacer) {
  CancelMidPresent();
  s_presenter_d3d = 0;
  if (pacer->mode == kHostPaceWaitable)
    pacer->mode = pacer->dwm_flush ? kHostPaceDwmFlush : kHostPaceTimer;
  HostFramePacerReset(pacer);
  snprintf(s_host_status, sizeof s_host_status,
           "Direct3D presenter lost (%.100s); using GDI on %s pacing",
           s_d3d.error, HostPaceModeName(pacer->mode));
  HostD3DShutdown();
  InvalidateRect(s_window, NULL, FALSE);
  UpdateDebugTitle();
}

static void HostFramePacerClose(HostFramePacer *pacer) {
  if (pacer->log) {
    HostPacingLogClose(pacer->async_log);
    pacer->async_log = NULL;
    pacer->log = NULL;
    s_audio_log_stats = 0;
  }
  if (pacer->timer) CloseHandle(pacer->timer);
  if (pacer->timer_resolution_active) timeEndPeriod(1);
  if (pacer->dwm_module) FreeLibrary(pacer->dwm_module);
}

int main(int argc, char **argv) {
  /* Before any window or device context exists: per-monitor DPI
   * awareness (otherwise DWM bitmap-stretches the window at >100%
   * scaling) and an opt-out from power throttling. */
  s_dpi_awareness_mode = HostInitDpiAwareness();
  HostDisablePowerThrottling();
  InitBuildIdentity();
  Dkc1DixieSetRomPath(argc > 1 ? argv[1] : "dkc1.sfc");
  /* Optional Dixie Kong Country mod: when the persisted setting is on, this
   * stock build hands the session to the sibling variant executable before
   * touching the ROM. */
  {
    char note[256];
    if (Dkc1DixieHandoffCheck(argc, argv, "dkc1_dixie_desktop.exe", note,
                              sizeof note)) {
      return 0; /* the variant executable owns the session */
    }
    if (note[0]) fprintf(stderr, "dixie mod: %s\n", note);

#ifdef DKC1_DIXIE_VARIANT
  /* The mod's sprite-DMA queue emits zero-size entries that the
   * hack's target emulator dropped; guard VRAM from the stomps.
   * (See dma_set_zero_size_vram_noop in snes/dma.h.) */
  dma_set_zero_size_vram_noop(1);
#endif
  }
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
#ifdef DKC1_DIXIE_VARIANT
  /* The variant synthesizes the modded ROM image from the clean ROM
   * argument (no patched-ROM file needed). */
  uint8_t *rom = Dkc1DixieLoadRom(rom_path, &rom_size, rom_error,
                                  sizeof rom_error);
#else
  uint8_t *rom =
      Dkc1ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
#endif
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
    char error[256];
    if (!Dkc1SramLoad(&s_sram_store, g_sram, (size_t)g_sram_size,
                      error, sizeof error)) {
      MessageBoxA(NULL, error, "Unable to load in-game saves", MB_ICONERROR);
      free(rom);
      return 13;
    }
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
  {
    const char *scaling = getenv("DKC1_SCALING");
    if (scaling && *scaling) {
      if (_stricmp(scaling, "nearest") == 0)
        s_scaling_mode = kHostScalingNearest;
      else if (_stricmp(scaling, "linear") == 0)
        s_scaling_mode = kHostScalingLinear;
      else
        s_scaling_mode = kHostScalingSharp;
    }
    s_square_pixels = EnvironmentEnabled("DKC1_SQUARE_PIXELS");
  }
  s_dpi = HostWindowDpi(NULL);  /* primary monitor until the window exists */
  s_window_scale = HostWindowScaleForDpi(s_dpi);
  RECT rect;
  {
    int width, height;
    HostWindowedClientSize(&width, &height);
    SetRect(&rect, 0, 0, width, height);
    AdjustWindowRectForHostDpi(&rect);
  }
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
  /* The window's own monitor may differ from the primary. */
  s_dpi = HostWindowDpi(s_window);
  s_window_scale = HostWindowScaleForDpi(s_dpi);
  ApplyWindowedSize();
  UpdateDebugTitle();

  memset(&s_bmi, 0, sizeof s_bmi);
  s_bmi.bmiHeader.biSize = sizeof s_bmi.bmiHeader;
  s_bmi.bmiHeader.biWidth = s_width;
  s_bmi.bmiHeader.biHeight = -s_height;  /* top-down */
  s_bmi.bmiHeader.biPlanes = 1;
  s_bmi.bmiHeader.biBitCount = 32;
  s_bmi.bmiHeader.biCompression = BI_RGB;

  AudioInit();

  {
    /* Direct3D 11 flip-model presenter unless DKC1_PRESENTER=gdi or the
     * device cannot be created; the GDI path stays as the fallback. */
    const char *presenter = getenv("DKC1_PRESENTER");
    const int want_gdi = presenter && _stricmp(presenter, "gdi") == 0;
    s_presenter_d3d = !want_gdi && HostD3DInit(s_window);
    if (!s_presenter_d3d) {
      if (!want_gdi && s_d3d.error[0])
        snprintf(s_host_status, sizeof s_host_status,
                 "GDI presenter (%.200s)", s_d3d.error);
      else if (want_gdi)
        snprintf(s_host_status, sizeof s_host_status,
                 "GDI presenter (DKC1_PRESENTER=gdi)");
    }
    if (EnvironmentEnabled("DKC1_FULLSCREEN")) SetFullscreen(1);
  }

  {
    /* Frame generation is a presentation option: off unless asked, so the
     * visible debugger's window captures remain real cartridge frames. */
    const char *framegen = getenv("DKC1_FRAMEGEN");
    s_framegen_enabled = framegen && *framegen && *framegen != '0';
    s_framegen_force = framegen && _stricmp(framegen, "force") == 0;
    if (s_framegen_enabled &&
        (EnvironmentEnabled("DKC1_WS_TRACE") ||
         EnvironmentEnabled("SNESRECOMP_WS_CACHE_LOG") ||
         EnvironmentEnabled("SNESRECOMP_WS_RETRODICT"))) {
      /* The in-between render serves margin tiles a second time, which
       * would double-count in the widescreen evidence taps. */
      s_framegen_enabled = 0;
      snprintf(s_host_status, sizeof s_host_status,
               "frame generation disabled: widescreen evidence taps armed");
    }
    const char *dump_start = getenv("DKC1_FRAMEGEN_DUMP_START");
    const char *dump_count = getenv("DKC1_FRAMEGEN_DUMP_COUNT");
    if (dump_start && *dump_start) {
      s_framegen_dump_start = strtol(dump_start, NULL, 10);
      s_framegen_dump_count =
          dump_count && *dump_count ? strtol(dump_count, NULL, 10) : 1;
    }
  }

  /* Raise the emulation thread before the pacer records its identity. */
  s_priority_mode = HostRaiseThreadPriority(&s_mmcss_handle);
  HostFramePacer pacer;
  if (!HostFramePacerInit(&pacer)) {
    MessageBoxA(s_window, "high-resolution clock unavailable",
                "DKC1Recomp", MB_ICONERROR);
    free(rom);
    return 5;
  }
  LARGE_INTEGER freq;
  freq = pacer.frequency;
  ApplyFrameGenSetting();
  UpdateDebugTitle();

  {
    /* Optional presenter warm-up.  About 1.5 s after a flip-model window
     * starts presenting, the desktop compositor changes its presentation
     * path once (a 60-80 ms Present stall was measured on this machine's
     * displays).  Evidence runs can absorb that transition before frame 1
     * by presenting the initial framebuffer for a while first; emulation,
     * audio and the pacing log all start afterwards. */
    const char *warm_text = getenv("DKC1_PRESENT_WARMUP_MS");
    long warm_ms = warm_text && *warm_text ? strtol(warm_text, NULL, 10) : 0;
    if (warm_ms > 5000) warm_ms = 5000;
    if (warm_ms > 0 && !s_paused) {
      const ULONGLONG until = GetTickCount64() + (ULONGLONG)warm_ms;
      while (s_running && GetTickCount64() < until) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
        HostFramePacerWaitFrame(&pacer, 0.0, 1);
        PresentFrame(s_pixels, 1, HostFramePacerSyncInterval(&pacer, 0));
        if (s_presenter_d3d && !s_d3d.active)
          HostFramePacerFallBackToGdi(&pacer);
      }
      HostFramePacerReset(&pacer);
      pacer.overruns = 0;
      snprintf(s_host_status, sizeof s_host_status,
               "presenter warmed for %ld ms before frame 1", warm_ms);
    }
  }

  while (s_running) {
    LARGE_INTEGER work_start;
    QueryPerformanceCounter(&work_start);
    double phase_tick = (double)work_start.QuadPart;
    pacer.loop_start_tick = phase_tick;
    {
      /* Time the pump and remember the slowest message: a blocking
       * handler shows up here rather than in work or wait. */
      MSG msg;
      LARGE_INTEGER pump_start, msg_start, msg_end;
      QueryPerformanceCounter(&pump_start);
      pacer.pending_slow_msg = 0;
      pacer.pending_slow_msg_ms = 0.0;
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        QueryPerformanceCounter(&msg_start);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        QueryPerformanceCounter(&msg_end);
        const double ms = (double)(msg_end.QuadPart - msg_start.QuadPart) *
                          1000.0 / (double)freq.QuadPart;
        if (ms > pacer.pending_slow_msg_ms) {
          pacer.pending_slow_msg_ms = ms;
          pacer.pending_slow_msg = msg.message;
        }
      }
      QueryPerformanceCounter(&msg_end);
      pacer.pending_pump_ms = (double)(msg_end.QuadPart - pump_start.QuadPart) *
                              1000.0 / (double)freq.QuadPart;
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
        if (loaded) {
          AudioResetTimeline();
          ForgetFrameGenHistory();
        }
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
      if (!save_op && accepted) {
        AudioResetTimeline();
        ForgetFrameGenHistory();
      }
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
      HostFramePacerReset(&pacer);
      HostFramePacerWaitFrame(&pacer, (double)work_start.QuadPart, 1);
      PresentFrame(s_smooth_valid ? s_smooth_pixels : s_pixels, 1,
                   HostFramePacerSyncInterval(&pacer, 0));
      if (s_presenter_d3d && !s_d3d.active)
        HostFramePacerFallBackToGdi(&pacer);
      continue;
    }

    Dkc1ScriptOps script_ops = {0};
    uint32_t input = 0;
    int run_frame = 1;
    int poll_manual = 0;
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
        ForgetFrameGenHistory();
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
      poll_manual = 1;
    }

    if (!run_frame) {
      UpdateDebugTitle();
      continue;
    }

    /* Compositor-locked modes wait for the slot first so the controller is
     * sampled as late as possible; timer mode and the 120 Hz frame
     * generation pair keep the deterministic work-first ordering. */
    const int work_first =
        pacer.mode == kHostPaceTimer || FrameGenExtraRefresh();
    if (!work_first) {
      HostFramePacerWaitFrame(&pacer, (double)work_start.QuadPart, 0);
      QueryPerformanceCounter(&work_start);
      phase_tick = (double)work_start.QuadPart;
    }
    if (poll_manual) input = PollInput();

    s_last_input = input;
    Dkc1DebugRecordInput(input);
    pacer.pending_setup_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    {
      const unsigned long long steps_before = interp_bridge_steps_total();
      const long tier_before = interp_tier_hit_count();
      const unsigned long long *banks = interp_bridge_bank_steps();
      memcpy(pacer.interp_bank_before, banks, sizeof pacer.interp_bank_before);
      RtlRunFrame(input);
      pacer.pending_interp_steps = interp_bridge_steps_total() - steps_before;
      pacer.pending_tier_hits = interp_tier_hit_count() - tier_before;
      pacer.pending_interp_bank = 0;
      pacer.pending_interp_bank_steps = 0;
      for (unsigned bank = 0; bank < 256; bank++) {
        const unsigned long long delta =
            banks[bank] - pacer.interp_bank_before[bank];
        if (delta > pacer.pending_interp_bank_steps) {
          pacer.pending_interp_bank_steps = delta;
          pacer.pending_interp_bank = bank;
        }
      }
    }
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
    {
      char error[256];
      if (!Dkc1SramFlush(&s_sram_store, g_sram, (size_t)g_sram_size,
                         false, error, sizeof error) && !s_sram_error_reported) {
        s_sram_error_reported = 1;
        s_paused = 1;
        MessageBoxA(s_window, error, "Unable to write in-game saves", MB_ICONERROR);
      }
    }
    Dkc1DrawPpuFrame();
    pacer.pending_render_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
    /* In-between frame: a second, host-only render of this frame's scene at
     * positions halfway back toward the previous frame. Skipped while a
     * click inspect or provenance overlay needs the real render's
     * provenance surface to stay intact. */
    int mid_valid = 0;
    s_smooth_valid = 0;
    s_framegen_last_valid = 0;
    memset(&s_framegen_stats, 0, sizeof s_framegen_stats);
    if (FrameGenActive() && !s_inspect_pending && !s_step_once &&
        !Dkc1DebugProvenanceOverlay() && Dkc1DebugLayerMask() == 0xff) {
      if (FrameGenExtraRefresh())
        mid_valid = Dkc1DrawInterpolatedFrame(s_mid_pixels, (size_t)s_width * 4,
                                            &s_framegen_stats) ? 1 : 0;
      Dkc1DrawSmoothedFrames(s_pixels, s_smooth_pixels, s_mid_pixels,
                            mid_valid != 0, &s_framegen_stats);
      mid_valid = FrameGenExtraRefresh();
      s_smooth_valid = 1;
      s_framegen_last_valid = mid_valid;
    }
    pacer.pending_interp_ms = HostFramePacerPhaseMs(&pacer, &phase_tick);
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
        s_present_fps_value = s_present_frames_window / elapsed;
        fps_frames = 0;
        s_present_frames_window = 0;
        fps_anchor = fps_now;
      }
    }

    /* The half-frame worker must have presented before this thread waits
     * for its own slot: that fixes the order F, F+0.5, F+1. */
    CollectMidPresent(&pacer);
    if (work_first)
      HostFramePacerWaitFrame(&pacer, (double)work_start.QuadPart, 1);
    HostFramePacerBeginPresent(&pacer, work_first);
    /* Only a frame that produced a midpoint splits its refreshes with it;
     * a bypassed interpolation (inspect, provenance, step, layer isolation)
     * presents the real image for the whole divisor. */
    const int mid_follows = FrameGenExtraRefresh() && mid_valid;
    PresentFrame(s_smooth_valid ? s_smooth_pixels : s_pixels, 1,
                 HostFramePacerSyncInterval(&pacer, mid_follows));
    HostFramePacerPresented(&pacer, s_host_frame);
    s_present_frames_window++;
    if (s_presenter_d3d && !s_d3d.active)
      HostFramePacerFallBackToGdi(&pacer);
    else if (mid_follows)
      ScheduleMidPresent(&pacer, s_mid_pixels);
    if (s_framegen_dump_count > 0 && s_host_frame >= s_framegen_dump_start &&
        s_host_frame < s_framegen_dump_start + s_framegen_dump_count) {
      /* Raw N-1/N plus delayed display F and its following midpoint F+0.5. */
      const char *dir = getenv("DKC1_FRAMEGEN_DUMP_DIR");
      if (!dir || !*dir) dir = "build/framegen-dump";
      _mkdir(dir);
      char path[1024];
      if (s_prev_pixels_valid) {
        snprintf(path, sizeof path, "%s/frame%06ld_prev.ppm", dir,
                 s_host_frame);
        WritePpm(path, s_prev_pixels, s_width, s_height);
      }
      if (mid_valid) {
        snprintf(path, sizeof path, "%s/frame%06ld_mid.ppm", dir,
                 s_host_frame);
        WritePpm(path, s_mid_pixels, s_width, s_height);
      }
      snprintf(path, sizeof path, "%s/frame%06ld_cur.ppm", dir, s_host_frame);
      WritePpm(path, s_pixels, s_width, s_height);
      snprintf(path, sizeof path, "%s/frame%06ld_display.ppm", dir, s_host_frame);
      WritePpm(path, s_smooth_valid ? s_smooth_pixels : s_pixels, s_width, s_height);
      snprintf(path, sizeof path, "%s/frame%06ld.json", dir, s_host_frame);
      FILE *meta = fopen(path, "wb");
      if (meta) {
        fprintf(meta,
                "{\"host_frame\":%ld,\"framegen\":%d,\"interp_valid\":%d,"
                "\"reject\":\"%s\",\"sprites_exact\":%u,"
                "\"sprites_actor\":%u,\"sprites_nearest\":%u,"
                "\"sprites_unmatched\":%u,\"actors_tracked\":%u,"
                "\"poses_changed\":%u,\"sprites_tween\":%u,"
                "\"tween_rects\":%u,\"pose_actors\":%u,\"pose_pixels\":%u,"
                "\"pose_mismatch\":%u,\"pose_source_frame\":%d,"
                "\"camera_dx\":%d,\"camera_dy\":%d,\"max_scroll_step\":%d}\n",
                s_host_frame, FrameGenActive(), mid_valid,
                s_framegen_stats.reject ? s_framegen_stats.reject : "",
                s_framegen_stats.sprites_exact,
                s_framegen_stats.sprites_actor,
                s_framegen_stats.sprites_nearest,
                s_framegen_stats.sprites_unmatched,
                s_framegen_stats.actors_tracked,
                s_framegen_stats.poses_changed,
                s_framegen_stats.sprites_tween, s_framegen_stats.tween_rects,
                s_framegen_stats.pose_actors, s_framegen_stats.pose_pixels,
                s_framegen_stats.pose_mismatch, s_framegen_stats.pose_source_frame,
                s_framegen_stats.camera_dx, s_framegen_stats.camera_dy,
                s_framegen_stats.max_scroll_step);
        fclose(meta);
      }
    }
    memcpy(s_prev_pixels, s_pixels, (size_t)s_width * (size_t)s_height * 4);
    s_prev_pixels_valid = 1;
    if ((s_host_frame % 60) == 0) UpdateDebugTitle();
    s_step_once = 0;

  }

  CloseMidPresenter();
  /* Flush the pacing evidence before any presenter teardown can fault. */
  const int report_hotspots = pacer.log != NULL || EnvironmentEnabled("DKC1_INTERP_HOTSPOTS");
  HostFramePacerClose(&pacer);
  if (report_hotspots)
    ReportInterpreterHotspots();
  HostD3DShutdown();
  HostSurfaceFree(&s_panel_surface);
  HostSurfaceFree(&s_badge_surface);
  HostReleaseThreadPriority(s_mmcss_handle);
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
  char save_error[256];
  bool saved = g_fail || !Dkc1LastLleResult() ||
      Dkc1SramFlush(&s_sram_store, g_sram, (size_t)g_sram_size,
                    true, save_error, sizeof save_error);
  if (!saved)
    MessageBoxA(NULL, save_error, "Unable to write in-game saves", MB_ICONERROR);
  free(rom);
  return saved ? 0 : 13;
}
