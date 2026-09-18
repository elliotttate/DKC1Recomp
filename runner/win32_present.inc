/* Win32 presentation geometry, GDI surfaces, and the Direct3D 11 flip-model
 * presenter.  Included by win32_host.c after its globals.
 *
 * Why this exists: the original host blitted with StretchDIBits on an
 * open-loop timer that aimed one millisecond ahead of a vblank estimate.
 * The GDI update itself took longer than that lead, the compositor samples
 * redirected GDI content some time after the vblank, and the period was
 * pinned from a single DWM reading that alternates between two values on
 * some displays.  None of that is visible to the host's own timers.
 *
 * The flip-model swap chain replaces the race with a queue: the host waits
 * on the swap chain's frame-latency waitable object (signalled when the
 * previous image has actually been consumed), then emulates, renders and
 * presents.  DXGI frame statistics report the refresh each image was shown
 * on, which is the Windows scanout oracle the pacing log lacked.
 *
 * Presentation applies the SNES 7:6 pixel aspect (same policy as the Mac
 * host) with a sharp-bilinear sampler by default, and the process is
 * per-monitor DPI aware so DWM never bitmap-stretches the window. */

#define COBJMACROS
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>

/* Shared engine policy for the SNES pixel aspect and viewport fit (7:6 by
 * default, with the same sub-source-pixel seam suppression the SDL/GL
 * hosts use), so every presenter frames the image identically. */
#include "desktop/display_aspect.h"

#define HOST_RELEASE(p) \
  do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

enum {
  kHostBadgeWidth = 96,
  kHostBadgeHeight = 22,
};

enum HostScalingMode {
  kHostScalingSharp = 0,   /* sharp bilinear: crisp texels, soft edges */
  kHostScalingNearest = 1,
  kHostScalingLinear = 2,
};

static int s_dpi = 96;
static int s_window_scale = 2;   /* windowed integer scale, DPI derived */
static int s_square_pixels;      /* 1: ignore the 7:6 pixel aspect */
static int s_scaling_mode = kHostScalingSharp;

/* ---- geometry ------------------------------------------------------ */

static int HostDpiScale(int value) { return MulDiv(value, s_dpi, 96); }

static int HostPanelWidth(void) { return HostDpiScale(kPanelWidth); }

static SnesDisplayAspect HostDisplayAspect(void) {
  return s_square_pixels ? kSnesDisplayAspect_SquarePixels8x7
                         : kSnesDisplayAspect_Crt4x3;
}

/* Width of `src_w` source columns presented at `scale` with the pixel
 * aspect applied. */
static int HostPresentWidth(int src_w, int scale) {
  int width = src_w * scale, height = s_height * scale;
  SnesDisplayAspect_ComputePresentationSize(src_w * scale, s_height * scale,
                                            HostDisplayAspect(), &width,
                                            &height);
  return width;
}

/* Destination rectangle of the game image inside a client area. Windowed
 * mode is an integer scale at the origin; fullscreen is the largest
 * aspect-preserving fit, centered, with the engine's seam suppression. */
static RECT HostGameRect(int client_w, int client_h) {
  RECT rect;
  if (!s_fullscreen) {
    SetRect(&rect, 0, 0, HostPresentWidth(s_width, s_window_scale),
            s_height * s_window_scale);
    return rect;
  }
  SnesDisplayViewport viewport;
  SnesDisplayAspect_ComputeViewport(s_width, s_height, client_w, client_h,
                                    HostDisplayAspect(), false, false,
                                    &viewport);
  SetRect(&rect, viewport.x, viewport.y, viewport.x + viewport.width,
          viewport.y + viewport.height);
  return rect;
}

/* Windowed client size: game view plus the optional debug panel. */
static void HostWindowedClientSize(int *width, int *height) {
  *width = HostPresentWidth(s_width, s_window_scale) +
           (s_panel_enabled ? HostPanelWidth() : 0);
  *height = s_height * s_window_scale;
}

/* Map a client-space click to a source pixel; -1 when outside the image. */
static void HostClientToGame(HWND hwnd, int click_x, int click_y,
                             int *game_x, int *game_y) {
  RECT client;
  *game_x = *game_y = -1;
  GetClientRect(hwnd, &client);
  const RECT dest = HostGameRect(client.right, client.bottom);
  const int dw = dest.right - dest.left, dh = dest.bottom - dest.top;
  if (dw <= 0 || dh <= 0) return;
  if (click_x < dest.left || click_x >= dest.right || click_y < dest.top ||
      click_y >= dest.bottom)
    return;
  *game_x = (int)((long long)(click_x - dest.left) * s_width / dw);
  *game_y = (int)((long long)(click_y - dest.top) * s_height / dh);
  if (*game_x >= s_width) *game_x = s_width - 1;
  if (*game_y >= s_height) *game_y = s_height - 1;
}

static int HostWindowScaleForDpi(int dpi) {
  const char *override = getenv("DKC1_WINDOW_SCALE");
  if (override && *override) {
    const int parsed = atoi(override);
    if (parsed >= 1 && parsed <= 8) return parsed;
  }
  int scale = (int)(2.0 * (double)dpi / 96.0 + 0.5);
  if (scale < 2) scale = 2;
  return scale;
}

/* ---- process setup: DPI awareness, priority, power ------------------ */

/* Per-monitor-V2 awareness (Windows 10 1703+), then the older per-monitor
 * API, then system awareness.  Without any of these, DWM stretches the
 * whole window by the display scale and every scroll step becomes an
 * uneven number of physical pixels. */
static const char *HostInitDpiAwareness(void) {
  HMODULE user32 = GetModuleHandleA("user32.dll");
  typedef BOOL (WINAPI *SetContextFn)(HANDLE);
  SetContextFn set_context = user32
      ? (SetContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext")
      : NULL;
  if (set_context && set_context((HANDLE)(INT_PTR)-4))
    return "per-monitor-v2";
  HMODULE shcore = LoadLibraryA("shcore.dll");
  typedef HRESULT (WINAPI *SetAwarenessFn)(int);
  SetAwarenessFn set_awareness = shcore
      ? (SetAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness")
      : NULL;
  if (set_awareness && SUCCEEDED(set_awareness(2)))
    return "per-monitor";
  if (SetProcessDPIAware()) return "system";
  return "unaware";
}

static int HostWindowDpi(HWND hwnd) {
  HMODULE user32 = GetModuleHandleA("user32.dll");
  typedef UINT (WINAPI *GetDpiFn)(HWND);
  GetDpiFn get_dpi = user32
      ? (GetDpiFn)GetProcAddress(user32, "GetDpiForWindow") : NULL;
  if (get_dpi && hwnd) {
    const UINT dpi = get_dpi(hwnd);
    if (dpi >= 48 && dpi <= 960) return (int)dpi;
  }
  HDC dc = GetDC(hwnd);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc) ReleaseDC(hwnd, dc);
  return dpi > 0 ? dpi : 96;
}

/* Multimedia Class Scheduler "Games" characteristics keep the emulation
 * thread from being descheduled through its present slot.  Falls back to
 * a plain priority raise where avrt.dll is unavailable. */
static HANDLE s_mmcss_handle;
static const char *HostRaiseThreadPriority(HANDLE *mmcss_out) {
  HMODULE avrt = LoadLibraryA("avrt.dll");
  typedef HANDLE (WINAPI *AvSetFn)(LPCSTR, LPDWORD);
  AvSetFn av_set = avrt
      ? (AvSetFn)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA") : NULL;
  DWORD task_index = 0;
  HANDLE handle = av_set ? av_set("Games", &task_index) : NULL;
  if (mmcss_out) *mmcss_out = handle;
  if (handle) return "mmcss-games";
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  return "above-normal";
}

static void HostReleaseThreadPriority(HANDLE mmcss) {
  if (!mmcss) return;
  HMODULE avrt = GetModuleHandleA("avrt.dll");
  typedef BOOL (WINAPI *AvRevertFn)(HANDLE);
  AvRevertFn av_revert = avrt
      ? (AvRevertFn)GetProcAddress(avrt, "AvRevertMmThreadCharacteristics")
      : NULL;
  if (av_revert) av_revert(mmcss);
}

/* Opt out of EcoQoS / power throttling so an unfocused or hybrid-CPU
 * scheduler does not move the frame loop onto efficiency cores. */
static void HostDisablePowerThrottling(void) {
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
  PROCESS_POWER_THROTTLING_STATE state;
  memset(&state, 0, sizeof state);
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  state.StateMask = 0;
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  typedef BOOL (WINAPI *SetInfoFn)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID,
                                   DWORD);
  SetInfoFn set_info = kernel32
      ? (SetInfoFn)GetProcAddress(kernel32, "SetProcessInformation") : NULL;
  if (set_info)
    set_info(GetCurrentProcess(), ProcessPowerThrottling, &state,
             sizeof state);
#endif
}

/* ---- GDI surfaces (debug panel, FPS badge) --------------------------- */

/* A top-down 32-bit DIB section: GDI text renders into it, and the pixels
 * are then either BitBlt (GDI presenter) or uploaded as a texture (D3D). */
typedef struct HostSurface {
  HDC dc;
  HBITMAP bitmap;
  HGDIOBJ previous;
  uint32_t *pixels;
  int width, height;
} HostSurface;

static void HostSurfaceFree(HostSurface *surface) {
  if (surface->dc && surface->previous)
    SelectObject(surface->dc, surface->previous);
  if (surface->bitmap) DeleteObject(surface->bitmap);
  if (surface->dc) DeleteDC(surface->dc);
  memset(surface, 0, sizeof *surface);
}

static int HostSurfaceEnsure(HostSurface *surface, int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  if (surface->dc && surface->width == width && surface->height == height)
    return 1;
  HostSurfaceFree(surface);
  BITMAPINFO info;
  memset(&info, 0, sizeof info);
  info.bmiHeader.biSize = sizeof info.bmiHeader;
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  surface->dc = CreateCompatibleDC(NULL);
  surface->bitmap = surface->dc
      ? CreateDIBSection(surface->dc, &info, DIB_RGB_COLORS, &bits, NULL, 0)
      : NULL;
  if (!surface->bitmap || !bits) {
    HostSurfaceFree(surface);
    return 0;
  }
  surface->previous = SelectObject(surface->dc, surface->bitmap);
  surface->pixels = (uint32_t *)bits;
  surface->width = width;
  surface->height = height;
  return 1;
}

/* DPI-sized fixed-pitch font for host text. */
static HFONT HostPanelFont(void) {
  static HFONT font;
  static int font_dpi;
  if (font && font_dpi == s_dpi) return font;
  if (font) DeleteObject(font);
  font = CreateFontA(-MulDiv(9, s_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     FIXED_PITCH | FF_MODERN, "Consolas");
  if (!font) font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
  font_dpi = s_dpi;
  return font;
}

/* ---- Direct3D 11 presenter ------------------------------------------ */

typedef struct HostD3DTexture {
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *view;
  int width, height;
} HostD3DTexture;

typedef struct HostD3DParams {
  float rect[4];      /* destination in NDC: x0, y0 (top), x1, y1 (bottom) */
  float tex_size[2];  /* source texels */
  float out_size[2];  /* destination pixels */
  float mode[4];      /* x: scaling mode */
} HostD3DParams;

typedef struct HostD3D {
  HMODULE d3d11_module, compiler_module;
  ID3D11Device *device;
  ID3D11DeviceContext *context;
  IDXGISwapChain1 *swapchain;
  IDXGISwapChain2 *swapchain2;
  HANDLE waitable;
  ID3D11RenderTargetView *target;
  ID3D11VertexShader *vertex_shader;
  ID3D11PixelShader *pixel_shader;
  ID3D11Buffer *constants;
  ID3D11SamplerState *sampler_linear, *sampler_point;
  ID3D11RasterizerState *rasterizer;
  HostD3DTexture frame, panel, badge;
  int width, height;          /* current back buffer size */
  int resize_pending;
  UINT swap_flags;
  UINT max_frame_latency;
  int flip_discard;
  int active;
  char error[160];
} HostD3D;

static HostD3D s_d3d;

static const char kHostShaderSource[] =
    "cbuffer Params : register(b0) {\n"
    "  float4 rect; float2 tex_size; float2 out_size; float4 mode; };\n"
    "Texture2D tex : register(t0);\n"
    "SamplerState samp_linear : register(s0);\n"
    "SamplerState samp_point : register(s1);\n"
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut vs_main(uint id : SV_VertexID) {\n"
    "  float2 uv = float2(id & 1, id >> 1);\n"
    "  VSOut o;\n"
    "  o.pos = float4(lerp(rect.x, rect.z, uv.x), lerp(rect.y, rect.w, uv.y),"
    " 0.0, 1.0);\n"
    "  o.uv = uv; return o; }\n"
    "float4 ps_main(VSOut i) : SV_Target {\n"
    "  if (mode.x == 1.0) return float4(tex.Sample(samp_point, i.uv).rgb, 1);\n"
    "  if (mode.x == 2.0) return float4(tex.Sample(samp_linear, i.uv).rgb, 1);\n"
    /* Sharp bilinear: nearest inside each texel, a one-output-pixel linear
     * transition at texel boundaries.  Fractional and 7:6 scales keep even
     * apparent column widths without nearest-neighbour shimmer. */
    "  float2 texel = i.uv * tex_size;\n"
    "  float2 scale = max(out_size / tex_size, 1.0);\n"
    "  float2 base = floor(texel);\n"
    "  float2 frac_part = texel - base;\n"
    "  float2 region = 0.5 - 0.5 / scale;\n"
    "  float2 center = frac_part - 0.5;\n"
    "  float2 f = (center - clamp(center, -region, region)) * scale + 0.5;\n"
    "  return float4(tex.Sample(samp_linear, (base + f) / tex_size).rgb, 1);"
    "}\n";

static void HostD3DTextureFree(HostD3DTexture *texture) {
  HOST_RELEASE(texture->view);
  HOST_RELEASE(texture->texture);
  texture->width = texture->height = 0;
}

static void HostD3DFail(const char *what, HRESULT hr) {
  snprintf(s_d3d.error, sizeof s_d3d.error, "%s failed (0x%08lx)", what,
           (unsigned long)hr);
}

static void HostD3DReleaseTarget(void) { HOST_RELEASE(s_d3d.target); }

static int HostD3DCreateTarget(void) {
  ID3D11Texture2D *back_buffer = NULL;
  HRESULT hr = IDXGISwapChain1_GetBuffer(s_d3d.swapchain, 0,
                                         &IID_ID3D11Texture2D,
                                         (void **)&back_buffer);
  if (FAILED(hr)) { HostD3DFail("GetBuffer", hr); return 0; }
  hr = ID3D11Device_CreateRenderTargetView(
      s_d3d.device, (ID3D11Resource *)back_buffer, NULL, &s_d3d.target);
  D3D11_TEXTURE2D_DESC desc;
  ID3D11Texture2D_GetDesc(back_buffer, &desc);
  s_d3d.width = (int)desc.Width;
  s_d3d.height = (int)desc.Height;
  HOST_RELEASE(back_buffer);
  if (FAILED(hr)) { HostD3DFail("CreateRenderTargetView", hr); return 0; }
  return 1;
}

static ID3D10Blob *HostD3DCompile(pD3DCompile compile, const char *entry,
                                  const char *target) {
  ID3D10Blob *code = NULL, *errors = NULL;
  HRESULT hr = compile(kHostShaderSource, sizeof kHostShaderSource - 1,
                       "dkc1_present", NULL, NULL, entry, target,
                       D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
  if (FAILED(hr)) {
    if (errors) {
      snprintf(s_d3d.error, sizeof s_d3d.error, "shader %s: %.120s", entry,
               (const char *)ID3D10Blob_GetBufferPointer(errors));
    } else {
      HostD3DFail(entry, hr);
    }
    HOST_RELEASE(code);
  }
  HOST_RELEASE(errors);
  return code;
}

static void HostD3DShutdown(void) {
  HostD3DTextureFree(&s_d3d.frame);
  HostD3DTextureFree(&s_d3d.panel);
  HostD3DTextureFree(&s_d3d.badge);
  HostD3DReleaseTarget();
  if (s_d3d.context) ID3D11DeviceContext_ClearState(s_d3d.context);
  HOST_RELEASE(s_d3d.rasterizer);
  HOST_RELEASE(s_d3d.sampler_point);
  HOST_RELEASE(s_d3d.sampler_linear);
  HOST_RELEASE(s_d3d.constants);
  HOST_RELEASE(s_d3d.pixel_shader);
  HOST_RELEASE(s_d3d.vertex_shader);
  if (s_d3d.waitable) CloseHandle(s_d3d.waitable);
  s_d3d.waitable = NULL;
  HOST_RELEASE(s_d3d.swapchain2);
  HOST_RELEASE(s_d3d.swapchain);
  HOST_RELEASE(s_d3d.context);
  HOST_RELEASE(s_d3d.device);
  /* d3d11.dll and the shader compiler stay loaded for the life of the
   * process: the runtime keeps worker threads alive briefly after the last
   * Release, and unloading the module underneath them faults at exit. */
  const int keep_error = s_d3d.error[0] != 0;
  char error[sizeof s_d3d.error];
  memcpy(error, s_d3d.error, sizeof error);
  memset(&s_d3d, 0, sizeof s_d3d);
  if (keep_error) memcpy(s_d3d.error, error, sizeof error);
}

/* Create the device, a flip-model swap chain with a frame-latency waitable
 * object (maximum latency one), shaders and samplers.  Returns 0 with
 * s_d3d.error filled in when the GDI path must be used instead. */
static int HostD3DInit(HWND hwnd) {
  static HMODULE d3d11_module, compiler_module;  /* never unloaded */
  memset(&s_d3d, 0, sizeof s_d3d);
  if (!d3d11_module) d3d11_module = LoadLibraryA("d3d11.dll");
  if (!compiler_module) compiler_module = LoadLibraryA("d3dcompiler_47.dll");
  s_d3d.d3d11_module = d3d11_module;
  s_d3d.compiler_module = compiler_module;
  if (!s_d3d.d3d11_module || !s_d3d.compiler_module) {
    snprintf(s_d3d.error, sizeof s_d3d.error, "%s unavailable",
             s_d3d.d3d11_module ? "d3dcompiler_47.dll" : "d3d11.dll");
    HostD3DShutdown();
    return 0;
  }
  PFN_D3D11_CREATE_DEVICE create_device = (PFN_D3D11_CREATE_DEVICE)
      GetProcAddress(s_d3d.d3d11_module, "D3D11CreateDevice");
  pD3DCompile compile =
      (pD3DCompile)GetProcAddress(s_d3d.compiler_module, "D3DCompile");
  if (!create_device || !compile) {
    snprintf(s_d3d.error, sizeof s_d3d.error, "D3D entry points missing");
    HostD3DShutdown();
    return 0;
  }
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
  HRESULT hr = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                             D3D11_SDK_VERSION, &s_d3d.device, &level,
                             &s_d3d.context);
  if (FAILED(hr)) {
    hr = create_device(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                       D3D11_SDK_VERSION, &s_d3d.device, &level,
                       &s_d3d.context);
  }
  if (FAILED(hr)) {
    HostD3DFail("D3D11CreateDevice", hr);
    HostD3DShutdown();
    return 0;
  }

  IDXGIDevice *dxgi_device = NULL;
  IDXGIAdapter *adapter = NULL;
  IDXGIFactory2 *factory = NULL;
  hr = ID3D11Device_QueryInterface(s_d3d.device, &IID_IDXGIDevice,
                                   (void **)&dxgi_device);
  if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
  if (SUCCEEDED(hr))
    hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2,
                                (void **)&factory);
  if (FAILED(hr)) {
    HostD3DFail("IDXGIFactory2", hr);
    HOST_RELEASE(adapter);
    HOST_RELEASE(dxgi_device);
    HostD3DShutdown();
    return 0;
  }

  DXGI_SWAP_CHAIN_DESC1 desc;
  memset(&desc, 0, sizeof desc);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 3;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  s_d3d.flip_discard = 1;
  hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)s_d3d.device,
                                            hwnd, &desc, NULL, NULL,
                                            &s_d3d.swapchain);
  if (FAILED(hr)) {
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    s_d3d.flip_discard = 0;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(
        factory, (IUnknown *)s_d3d.device, hwnd, &desc, NULL, NULL,
        &s_d3d.swapchain);
  }
  if (SUCCEEDED(hr)) {
    s_d3d.swap_flags = desc.Flags;
    /* The host owns Alt+Enter and window changes. */
    IDXGIFactory2_MakeWindowAssociation(
        factory, hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
  }
  HOST_RELEASE(factory);
  HOST_RELEASE(adapter);
  HOST_RELEASE(dxgi_device);
  if (FAILED(hr)) {
    HostD3DFail("CreateSwapChainForHwnd", hr);
    HostD3DShutdown();
    return 0;
  }
  hr = IDXGISwapChain1_QueryInterface(s_d3d.swapchain, &IID_IDXGISwapChain2,
                                      (void **)&s_d3d.swapchain2);
  s_d3d.max_frame_latency = 1;
  const char *latency = getenv("DKC1_MAX_FRAME_LATENCY");
  if (latency && strcmp(latency, "2") == 0) s_d3d.max_frame_latency = 2;
  if (SUCCEEDED(hr))
    hr = IDXGISwapChain2_SetMaximumFrameLatency(s_d3d.swapchain2,
                                               s_d3d.max_frame_latency);
  if (SUCCEEDED(hr))
    s_d3d.waitable =
        IDXGISwapChain2_GetFrameLatencyWaitableObject(s_d3d.swapchain2);
  if (FAILED(hr) || !s_d3d.waitable) {
    HostD3DFail("frame latency waitable object", hr);
    HostD3DShutdown();
    return 0;
  }
  if (!HostD3DCreateTarget()) {
    HostD3DShutdown();
    return 0;
  }

  ID3D10Blob *vs_code = HostD3DCompile(compile, "vs_main", "vs_4_0");
  ID3D10Blob *ps_code = HostD3DCompile(compile, "ps_main", "ps_4_0");
  if (vs_code)
    hr = ID3D11Device_CreateVertexShader(
        s_d3d.device, ID3D10Blob_GetBufferPointer(vs_code),
        ID3D10Blob_GetBufferSize(vs_code), NULL, &s_d3d.vertex_shader);
  if (SUCCEEDED(hr) && ps_code)
    hr = ID3D11Device_CreatePixelShader(
        s_d3d.device, ID3D10Blob_GetBufferPointer(ps_code),
        ID3D10Blob_GetBufferSize(ps_code), NULL, &s_d3d.pixel_shader);
  const int shaders_ok = vs_code && ps_code && SUCCEEDED(hr);
  HOST_RELEASE(vs_code);
  HOST_RELEASE(ps_code);
  if (!shaders_ok) {
    if (!s_d3d.error[0]) HostD3DFail("shader objects", hr);
    HostD3DShutdown();
    return 0;
  }

  D3D11_BUFFER_DESC buffer_desc;
  memset(&buffer_desc, 0, sizeof buffer_desc);
  buffer_desc.ByteWidth = sizeof(HostD3DParams);
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = ID3D11Device_CreateBuffer(s_d3d.device, &buffer_desc, NULL,
                                 &s_d3d.constants);
  D3D11_SAMPLER_DESC sampler_desc;
  memset(&sampler_desc, 0, sizeof sampler_desc);
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  if (SUCCEEDED(hr))
    hr = ID3D11Device_CreateSamplerState(s_d3d.device, &sampler_desc,
                                         &s_d3d.sampler_linear);
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  if (SUCCEEDED(hr))
    hr = ID3D11Device_CreateSamplerState(s_d3d.device, &sampler_desc,
                                         &s_d3d.sampler_point);
  D3D11_RASTERIZER_DESC raster_desc;
  memset(&raster_desc, 0, sizeof raster_desc);
  raster_desc.FillMode = D3D11_FILL_SOLID;
  raster_desc.CullMode = D3D11_CULL_NONE;
  raster_desc.DepthClipEnable = TRUE;
  if (SUCCEEDED(hr))
    hr = ID3D11Device_CreateRasterizerState(s_d3d.device, &raster_desc,
                                            &s_d3d.rasterizer);
  if (FAILED(hr)) {
    HostD3DFail("pipeline state", hr);
    HostD3DShutdown();
    return 0;
  }
  s_d3d.active = 1;
  return 1;
}

static void HostD3DRequestResize(void) {
  if (s_d3d.active) s_d3d.resize_pending = 1;
}

/* Upload a top-down BGRA image into a dynamic texture, recreating it when
 * the size changes.  Callers hold the present lock. */
static int HostD3DUpload(HostD3DTexture *texture, const void *pixels,
                         int width, int height, size_t pitch) {
  if (!s_d3d.active || width < 1 || height < 1 || !pixels) return 0;
  if (!texture->texture || texture->width != width ||
      texture->height != height) {
    HostD3DTextureFree(texture);
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof desc);
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = ID3D11Device_CreateTexture2D(s_d3d.device, &desc, NULL,
                                              &texture->texture);
    if (SUCCEEDED(hr))
      hr = ID3D11Device_CreateShaderResourceView(
          s_d3d.device, (ID3D11Resource *)texture->texture, NULL,
          &texture->view);
    if (FAILED(hr)) {
      HostD3DTextureFree(texture);
      return 0;
    }
    texture->width = width;
    texture->height = height;
  }
  D3D11_MAPPED_SUBRESOURCE mapped;
  HRESULT hr = ID3D11DeviceContext_Map(s_d3d.context,
                                       (ID3D11Resource *)texture->texture, 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) return 0;
  const size_t row_bytes = (size_t)width * 4;
  for (int y = 0; y < height; y++) {
    memcpy((uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
           (const uint8_t *)pixels + (size_t)y * pitch, row_bytes);
  }
  ID3D11DeviceContext_Unmap(s_d3d.context, (ID3D11Resource *)texture->texture,
                            0);
  return 1;
}

/* Bind the back buffer (resizing first when the window changed) and clear
 * it.  Returns the client size in *width/*height. */
static int HostD3DBeginFrame(int *width, int *height, const float clear[4]) {
  if (!s_d3d.active) return 0;
  if (s_d3d.resize_pending) {
    s_d3d.resize_pending = 0;
    HostD3DReleaseTarget();
    ID3D11DeviceContext_ClearState(s_d3d.context);
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(s_d3d.swapchain, 0, 0, 0,
                                               DXGI_FORMAT_UNKNOWN,
                                               s_d3d.swap_flags);
    if (FAILED(hr) || !HostD3DCreateTarget()) {
      if (!s_d3d.error[0]) HostD3DFail("ResizeBuffers", hr);
      s_d3d.active = 0;
      return 0;
    }
  }
  if (!s_d3d.target) return 0;
  ID3D11DeviceContext_OMSetRenderTargets(s_d3d.context, 1, &s_d3d.target,
                                         NULL);
  D3D11_VIEWPORT viewport;
  memset(&viewport, 0, sizeof viewport);
  viewport.Width = (float)s_d3d.width;
  viewport.Height = (float)s_d3d.height;
  viewport.MaxDepth = 1.0f;
  ID3D11DeviceContext_RSSetViewports(s_d3d.context, 1, &viewport);
  ID3D11DeviceContext_ClearRenderTargetView(s_d3d.context, s_d3d.target,
                                            clear);
  ID3D11DeviceContext_IASetInputLayout(s_d3d.context, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(
      s_d3d.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  ID3D11DeviceContext_VSSetShader(s_d3d.context, s_d3d.vertex_shader, NULL, 0);
  ID3D11DeviceContext_PSSetShader(s_d3d.context, s_d3d.pixel_shader, NULL, 0);
  ID3D11DeviceContext_RSSetState(s_d3d.context, s_d3d.rasterizer);
  ID3D11SamplerState *samplers[2] = {s_d3d.sampler_linear,
                                     s_d3d.sampler_point};
  ID3D11DeviceContext_PSSetSamplers(s_d3d.context, 0, 2, samplers);
  ID3D11DeviceContext_VSSetConstantBuffers(s_d3d.context, 0, 1,
                                           &s_d3d.constants);
  ID3D11DeviceContext_PSSetConstantBuffers(s_d3d.context, 0, 1,
                                           &s_d3d.constants);
  *width = s_d3d.width;
  *height = s_d3d.height;
  return 1;
}

/* Draw one texture into a client-space rectangle. */
static void HostD3DDrawQuad(const HostD3DTexture *texture, const RECT *dest,
                            int mode) {
  if (!s_d3d.active || !texture->view || s_d3d.width < 1 || s_d3d.height < 1)
    return;
  HostD3DParams params;
  const float cw = (float)s_d3d.width, ch = (float)s_d3d.height;
  params.rect[0] = (float)dest->left / cw * 2.0f - 1.0f;
  params.rect[1] = 1.0f - (float)dest->top / ch * 2.0f;
  params.rect[2] = (float)dest->right / cw * 2.0f - 1.0f;
  params.rect[3] = 1.0f - (float)dest->bottom / ch * 2.0f;
  params.tex_size[0] = (float)texture->width;
  params.tex_size[1] = (float)texture->height;
  params.out_size[0] = (float)(dest->right - dest->left);
  params.out_size[1] = (float)(dest->bottom - dest->top);
  params.mode[0] = (float)mode;
  params.mode[1] = params.mode[2] = params.mode[3] = 0.0f;
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(ID3D11DeviceContext_Map(s_d3d.context,
                                     (ID3D11Resource *)s_d3d.constants, 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    return;
  memcpy(mapped.pData, &params, sizeof params);
  ID3D11DeviceContext_Unmap(s_d3d.context, (ID3D11Resource *)s_d3d.constants,
                            0);
  ID3D11ShaderResourceView *view = texture->view;
  ID3D11DeviceContext_PSSetShaderResources(s_d3d.context, 0, 1, &view);
  ID3D11DeviceContext_Draw(s_d3d.context, 4, 0);
}

/* Present with the given sync interval.  A removed device deactivates the
 * presenter so the host can fall back to GDI for the rest of the run. */
static HRESULT HostD3DPresent(UINT sync_interval) {
  if (!s_d3d.active) return E_FAIL;
  ID3D11ShaderResourceView *none = NULL;
  ID3D11DeviceContext_PSSetShaderResources(s_d3d.context, 0, 1, &none);
  HRESULT hr = IDXGISwapChain1_Present(s_d3d.swapchain, sync_interval, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    HostD3DFail("Present (device lost)", hr);
    s_d3d.active = 0;
  }
  return hr;
}

/* Block until the swap chain can accept the next image.  Returns 1 when
 * signalled, 0 on timeout (a host stall, never a reason to catch up). */
static int HostD3DWaitForSlot(DWORD timeout_ms) {
  if (!s_d3d.active || !s_d3d.waitable) return 0;
  return WaitForSingleObjectEx(s_d3d.waitable, timeout_ms, FALSE) ==
         WAIT_OBJECT_0;
}

typedef struct HostScanoutStats {
  UINT present_count;        /* our latest Present call number */
  UINT displayed_count;      /* PresentCount of the last displayed image */
  UINT displayed_refresh;    /* refresh on which it was displayed */
  UINT sync_refresh;         /* refresh count at sync_qpc */
  LONGLONG sync_qpc;         /* QPC of that refresh */
  int disjoint;              /* statistics reset (mode/monitor change) */
  int valid;
} HostScanoutStats;

/* DXGI per-swap-chain statistics: which refresh each image landed on. */
static void HostD3DScanout(HostScanoutStats *out) {
  memset(out, 0, sizeof *out);
  if (!s_d3d.active) return;
  UINT count = 0;
  if (SUCCEEDED(IDXGISwapChain1_GetLastPresentCount(s_d3d.swapchain, &count)))
    out->present_count = count;
  DXGI_FRAME_STATISTICS stats;
  memset(&stats, 0, sizeof stats);
  HRESULT hr = IDXGISwapChain1_GetFrameStatistics(s_d3d.swapchain, &stats);
  if (hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
    out->disjoint = 1;
    return;
  }
  if (FAILED(hr)) return;
  out->displayed_count = stats.PresentCount;
  out->displayed_refresh = stats.PresentRefreshCount;
  out->sync_refresh = stats.SyncRefreshCount;
  out->sync_qpc = stats.SyncQPCTime.QuadPart;
  out->valid = 1;
}
