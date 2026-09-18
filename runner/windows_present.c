/* Windows presentation, pacing and frame generation for the SDL host.
 * See windows_present.h. The Direct3D presenter, pacer and half-frame worker
 * follow the native Win32 host (win32_host.c, win32_present.inc,
 * win32_mid_present.inc); the graphics passes are the Metal source
 * translated to HLSL by scripts/generate_windows_hlsl.py. */
#define COBJMACROS
/* Windows headers first: the engine's types.h re-defines HIBYTE over the
 * windef.h macro, and the reverse order would warn under /W4 /WX. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windows_present.h"
#include "windows_platform.h"
#include "dkc1_framegen.h"
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "desktop_crt.h"
#include "snes/interp_bridge.h"
#include "windows_hlsl.h"
#include "win32_present_cadence.h"
#include "win32_pacing_log.inc"

#define RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

enum { kUniformCount = 27, kMaxPixelBytes = kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4 };
enum { Flat, Finish, Reconstruct, Lines, Beam, Down, Blur, Compose, PassCount };
static const char *const kPassNames[PassCount] = {
  "dkc1_flat", "dkc1_finish", "dkc1_reconstruct", "dkc1_lines",
  "dkc1_beam", "dkc1_down", "dkc1_blur", "dkc1_compose"};

typedef struct Target {
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *view;
  ID3D11RenderTargetView *target;
  int w, h;
} Target;

typedef struct Presenter {
  SDL_Window *sdl;
  HWND window;
  int d3d;                     /* 1: Direct3D 11 active; 0: OpenGL fallback */
  int gl;
  ID3D11Device *device;
  ID3D11DeviceContext *context;
  IDXGISwapChain1 *swapchain;
  IDXGISwapChain2 *swapchain2;
  HANDLE waitable;
  ID3D11RenderTargetView *backbuffer;
  ID3D11VertexShader *vertex;
  ID3D11PixelShader *pixel[PassCount];
  ID3D11Buffer *constants;
  ID3D11SamplerState *samplers[2];
  ID3D11RasterizerState *rasterizer;
  Target input, lines, beam, glow[2], halo[2];
  int width, height;           /* back buffer */
  UINT swap_flags;
  UINT max_frame_latency;
  char error[200];
} Presenter;

static Presenter s_p;
static SRWLOCK s_lock = SRWLOCK_INIT;
static int s_minimized;

/* ---- Direct3D 11 -------------------------------------------------------- */

static void Fail(const char *what, HRESULT hr) {
  snprintf(s_p.error, sizeof s_p.error, "%s failed (0x%08lx)", what, (unsigned long)hr);
}

static void TargetFree(Target *t) {
  RELEASE(t->target); RELEASE(t->view); RELEASE(t->texture); t->w = t->h = 0;
}

static int TargetEnsure(Target *t, int w, int h, int input) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (t->texture && t->w == w && t->h == h) return 1;
  TargetFree(t);
  D3D11_TEXTURE2D_DESC desc;
  memset(&desc, 0, sizeof desc);
  desc.Width = (UINT)w; desc.Height = (UINT)h; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = input ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Usage = input ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (input ? 0 : D3D11_BIND_RENDER_TARGET);
  desc.CPUAccessFlags = input ? D3D11_CPU_ACCESS_WRITE : 0;
  HRESULT hr = ID3D11Device_CreateTexture2D(s_p.device, &desc, NULL, &t->texture);
  if (SUCCEEDED(hr))
    hr = ID3D11Device_CreateShaderResourceView(s_p.device, (ID3D11Resource *)t->texture, NULL, &t->view);
  if (SUCCEEDED(hr) && !input)
    hr = ID3D11Device_CreateRenderTargetView(s_p.device, (ID3D11Resource *)t->texture, NULL, &t->target);
  if (FAILED(hr)) { Fail("texture", hr); TargetFree(t); return 0; }
  t->w = w; t->h = h;
  return 1;
}

static int UploadInput(const uint8_t *pixels, int w, int h) {
  if (!TargetEnsure(&s_p.input, w, h, 1)) return 0;
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(ID3D11DeviceContext_Map(s_p.context, (ID3D11Resource *)s_p.input.texture, 0,
                                     D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    return 0;
  for (int y = 0; y < h; y++)
    memcpy((uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch, pixels + (size_t)y * w * 4, (size_t)w * 4);
  ID3D11DeviceContext_Unmap(s_p.context, (ID3D11Resource *)s_p.input.texture, 0);
  return 1;
}

static void ReleaseBackbuffer(void) { RELEASE(s_p.backbuffer); }

static int CreateBackbuffer(void) {
  ID3D11Texture2D *back = NULL;
  HRESULT hr = IDXGISwapChain1_GetBuffer(s_p.swapchain, 0, &IID_ID3D11Texture2D, (void **)&back);
  if (FAILED(hr)) { Fail("GetBuffer", hr); return 0; }
  hr = ID3D11Device_CreateRenderTargetView(s_p.device, (ID3D11Resource *)back, NULL, &s_p.backbuffer);
  D3D11_TEXTURE2D_DESC desc;
  ID3D11Texture2D_GetDesc(back, &desc);
  s_p.width = (int)desc.Width; s_p.height = (int)desc.Height;
  RELEASE(back);
  if (FAILED(hr)) { Fail("CreateRenderTargetView", hr); return 0; }
  return 1;
}

static void D3DShutdown(void) {
  Target *targets[] = {&s_p.input, &s_p.lines, &s_p.beam, &s_p.glow[0], &s_p.glow[1], &s_p.halo[0], &s_p.halo[1]};
  for (size_t i = 0; i < sizeof targets / sizeof *targets; i++) TargetFree(targets[i]);
  ReleaseBackbuffer();
  if (s_p.context) ID3D11DeviceContext_ClearState(s_p.context);
  RELEASE(s_p.rasterizer);
  for (int i = 0; i < 2; i++) RELEASE(s_p.samplers[i]);
  RELEASE(s_p.constants);
  for (int i = 0; i < PassCount; i++) RELEASE(s_p.pixel[i]);
  RELEASE(s_p.vertex);
  if (s_p.waitable) CloseHandle(s_p.waitable);
  s_p.waitable = NULL;
  RELEASE(s_p.swapchain2);
  RELEASE(s_p.swapchain);
  RELEASE(s_p.context);
  RELEASE(s_p.device);
  s_p.d3d = 0;
}

static ID3D10Blob *Compile(pD3DCompile compile, const char *entry, const char *target) {
  ID3D10Blob *code = NULL, *errors = NULL;
  HRESULT hr = compile(kWindowsHlslSource, sizeof kWindowsHlslSource - 1, "dkc1_present", NULL, NULL,
                       entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
  if (FAILED(hr)) {
    if (errors)
      snprintf(s_p.error, sizeof s_p.error, "shader %s: %.150s", entry,
               (const char *)ID3D10Blob_GetBufferPointer(errors));
    else
      Fail(entry, hr);
    RELEASE(code);
  }
  RELEASE(errors);
  return code;
}

/* Device, flip-model swap chain with a frame-latency waitable object
 * (maximum latency one), the translated passes, samplers and state. */
static int D3DInit(HWND hwnd) {
  static HMODULE d3d11_module, compiler_module;  /* never unloaded */
  if (!d3d11_module) d3d11_module = LoadLibraryA("d3d11.dll");
  if (!compiler_module) compiler_module = LoadLibraryA("d3dcompiler_47.dll");
  if (!d3d11_module || !compiler_module) {
    snprintf(s_p.error, sizeof s_p.error, "%s unavailable", d3d11_module ? "d3dcompiler_47.dll" : "d3d11.dll");
    return 0;
  }
  PFN_D3D11_CREATE_DEVICE create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11_module, "D3D11CreateDevice");
  pD3DCompile compile = (pD3DCompile)GetProcAddress(compiler_module, "D3DCompile");
  if (!create || !compile) { snprintf(s_p.error, sizeof s_p.error, "D3D entry points missing"); return 0; }
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
  HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                      D3D11_SDK_VERSION, &s_p.device, &level, &s_p.context);
  if (FAILED(hr))
    hr = create(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                D3D11_SDK_VERSION, &s_p.device, &level, &s_p.context);
  if (FAILED(hr)) { Fail("D3D11CreateDevice", hr); D3DShutdown(); return 0; }

  IDXGIDevice *dxgi = NULL; IDXGIAdapter *adapter = NULL; IDXGIFactory2 *factory = NULL;
  hr = ID3D11Device_QueryInterface(s_p.device, &IID_IDXGIDevice, (void **)&dxgi);
  if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi, &adapter);
  if (SUCCEEDED(hr)) hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
  if (FAILED(hr)) { Fail("IDXGIFactory2", hr); RELEASE(adapter); RELEASE(dxgi); D3DShutdown(); return 0; }
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
  hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)s_p.device, hwnd, &desc, NULL, NULL, &s_p.swapchain);
  if (FAILED(hr)) {
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)s_p.device, hwnd, &desc, NULL, NULL, &s_p.swapchain);
  }
  if (SUCCEEDED(hr)) {
    s_p.swap_flags = desc.Flags;
    /* SDL owns Alt+Enter and window changes. */
    IDXGIFactory2_MakeWindowAssociation(factory, hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
  }
  RELEASE(factory); RELEASE(adapter); RELEASE(dxgi);
  if (FAILED(hr)) { Fail("CreateSwapChainForHwnd", hr); D3DShutdown(); return 0; }
  hr = IDXGISwapChain1_QueryInterface(s_p.swapchain, &IID_IDXGISwapChain2, (void **)&s_p.swapchain2);
  s_p.max_frame_latency = 1;
  const char *latency = getenv("DKC1_MAX_FRAME_LATENCY");
  if (latency && strcmp(latency, "2") == 0) s_p.max_frame_latency = 2;
  if (SUCCEEDED(hr)) hr = IDXGISwapChain2_SetMaximumFrameLatency(s_p.swapchain2, s_p.max_frame_latency);
  if (SUCCEEDED(hr)) s_p.waitable = IDXGISwapChain2_GetFrameLatencyWaitableObject(s_p.swapchain2);
  if (FAILED(hr) || !s_p.waitable) { Fail("frame latency waitable object", hr); D3DShutdown(); return 0; }
  if (!CreateBackbuffer()) { D3DShutdown(); return 0; }

  ID3D10Blob *code = Compile(compile, "dkc1_vertex", "vs_4_0");
  hr = code ? ID3D11Device_CreateVertexShader(s_p.device, ID3D10Blob_GetBufferPointer(code),
                                              ID3D10Blob_GetBufferSize(code), NULL, &s_p.vertex) : E_FAIL;
  RELEASE(code);
  if (FAILED(hr)) { if (!s_p.error[0]) Fail("vertex shader", hr); D3DShutdown(); return 0; }
  for (int i = 0; i < PassCount; i++) {
    int found = 0;
    for (int n = 0; n < kWindowsHlslPassCount; n++) found |= strcmp(kWindowsHlslPasses[n], kPassNames[i]) == 0;
    if (!found) { snprintf(s_p.error, sizeof s_p.error, "pass %s missing from translation", kPassNames[i]); D3DShutdown(); return 0; }
    code = Compile(compile, kPassNames[i], "ps_4_0");
    hr = code ? ID3D11Device_CreatePixelShader(s_p.device, ID3D10Blob_GetBufferPointer(code),
                                               ID3D10Blob_GetBufferSize(code), NULL, &s_p.pixel[i]) : E_FAIL;
    RELEASE(code);
    if (FAILED(hr)) { if (!s_p.error[0]) Fail(kPassNames[i], hr); D3DShutdown(); return 0; }
  }
  D3D11_BUFFER_DESC bd;
  memset(&bd, 0, sizeof bd);
  bd.ByteWidth = kUniformCount * 16;  /* one float per 16-byte constant slot */
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = ID3D11Device_CreateBuffer(s_p.device, &bd, NULL, &s_p.constants);
  D3D11_SAMPLER_DESC sd;
  memset(&sd, 0, sizeof sd);
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateSamplerState(s_p.device, &sd, &s_p.samplers[0]);
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateSamplerState(s_p.device, &sd, &s_p.samplers[1]);
  D3D11_RASTERIZER_DESC rd;
  memset(&rd, 0, sizeof rd);
  rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(s_p.device, &rd, &s_p.rasterizer);
  if (FAILED(hr)) { Fail("pipeline state", hr); D3DShutdown(); return 0; }
  s_p.d3d = 1;
  s_p.error[0] = 0;
  return 1;
}

/* Bind the back buffer, resizing it first when the client area changed. */
static int BeginFrame(int *width, int *height) {
  if (!s_p.d3d) return 0;
  RECT client;
  GetClientRect(s_p.window, &client);
  if (client.right > 0 && client.bottom > 0 && (client.right != s_p.width || client.bottom != s_p.height)) {
    ReleaseBackbuffer();
    ID3D11DeviceContext_ClearState(s_p.context);
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(s_p.swapchain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, s_p.swap_flags);
    if (FAILED(hr) || !CreateBackbuffer()) {
      if (!s_p.error[0]) Fail("ResizeBuffers", hr);
      s_p.d3d = 0;
      return 0;
    }
  }
  if (!s_p.backbuffer) return 0;
  static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  ID3D11DeviceContext_ClearRenderTargetView(s_p.context, s_p.backbuffer, black);
  ID3D11DeviceContext_IASetInputLayout(s_p.context, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(s_p.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  ID3D11DeviceContext_VSSetShader(s_p.context, s_p.vertex, NULL, 0);
  ID3D11DeviceContext_RSSetState(s_p.context, s_p.rasterizer);
  ID3D11DeviceContext_PSSetSamplers(s_p.context, 0, 2, s_p.samplers);
  ID3D11DeviceContext_VSSetConstantBuffers(s_p.context, 0, 1, &s_p.constants);
  ID3D11DeviceContext_PSSetConstantBuffers(s_p.context, 0, 1, &s_p.constants);
  *width = s_p.width; *height = s_p.height;
  return 1;
}

/* One pass: `source` (plus glow/halo for Compose) into `target`, or the back
 * buffer when NULL, over the viewport rectangle. u[0..1] follow the source. */
static int Pass(int index, const Target *source, const Target *target, int x, int y, int w, int h, float *u) {
  ID3D11RenderTargetView *rtv = target ? target->target : s_p.backbuffer;
  ID3D11ShaderResourceView *none[3] = {NULL, NULL, NULL};
  ID3D11DeviceContext_PSSetShaderResources(s_p.context, 0, 3, none);
  ID3D11DeviceContext_OMSetRenderTargets(s_p.context, 1, &rtv, NULL);
  D3D11_VIEWPORT viewport;
  memset(&viewport, 0, sizeof viewport);
  viewport.TopLeftX = (float)x; viewport.TopLeftY = (float)y;
  viewport.Width = (float)w; viewport.Height = (float)h; viewport.MaxDepth = 1.0f;
  ID3D11DeviceContext_RSSetViewports(s_p.context, 1, &viewport);
  u[0] = (float)source->w; u[1] = (float)source->h;
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(ID3D11DeviceContext_Map(s_p.context, (ID3D11Resource *)s_p.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    return 0;
  float *slots = (float *)mapped.pData;
  memset(slots, 0, kUniformCount * 16);
  for (int i = 0; i < kUniformCount; i++) slots[i * 4] = u[i];
  ID3D11DeviceContext_Unmap(s_p.context, (ID3D11Resource *)s_p.constants, 0);
  ID3D11DeviceContext_PSSetShader(s_p.context, s_p.pixel[index], NULL, 0);
  ID3D11ShaderResourceView *views[3] = {source->view, NULL, NULL};
  if (index == Compose) { views[1] = s_p.glow[0].view; views[2] = s_p.halo[0].view; }
  ID3D11DeviceContext_PSSetShaderResources(s_p.context, 0, 3, views);
  ID3D11DeviceContext_Draw(s_p.context, 4, 0);
  ID3D11DeviceContext_PSSetShaderResources(s_p.context, 0, 3, none);
  return 1;
}

/* The Mac/OpenGL pass sequence: flat/reconstruct straight to the back
 * buffer, or the CRT television chain lines -> beam -> glow/halo -> compose.
 * Callers hold s_lock. */
static int RenderD3D(const uint8_t *pixels, int w, int h, int display_width, const Dkc1GraphicsSettings *settings) {
  int ow, oh;
  if (!BeginFrame(&ow, &oh) || ow < 1 || oh < 1) return 0;
  int vw = ow, vh = ow * h / display_width;
  if (vh > oh) { vh = oh; vw = oh * display_width / h; }
  const int x = (ow - vw) / 2, y = (oh - vh) / 2;
  if (!UploadInput(pixels, w, h)) return 0;
  float u[kUniformCount] = {(float)w, (float)h, (float)vw, (float)vh, (float)settings->reconstruct_mode,
    settings->strength / 100.f, settings->softness / 100.f, settings->shading / 100.f};
  Dkc1CrtFrameParams c;
  int ok = 1;
  if (settings->display == kDkc1DisplayCrt && Dkc1CrtDerive(&settings->crt, vw, vh, w, h, &c)) {
    const int gw = vw / 4 > 0 ? vw / 4 : 1, gh = vh / 4 > 0 ? vh / 4 : 1;
    const int hw = vw / 16 > 0 ? vw / 16 : 1, hh = vh / 16 > 0 ? vh / 16 : 1;
    ok = TargetEnsure(&s_p.lines, vw, h, 0) && TargetEnsure(&s_p.beam, vw, vh, 0);
    for (int i = 0; i < 2; i++) ok = ok && TargetEnsure(&s_p.glow[i], gw, gh, 0) && TargetEnsure(&s_p.halo[i], hw, hh, 0);
    u[8] = c.sigma_h; u[9] = c.sigma_dark; u[10] = c.sigma_bright; u[11] = c.beam_fade;
    u[14] = c.glow; u[15] = c.halation; u[16] = c.curvature_x; u[17] = c.curvature_y;
    u[18] = c.corner_radius; u[19] = c.vignette;
    u[20] = c.mask == kDkc1CrtMaskNone ? 0.f : c.mask == kDkc1CrtMaskSlot ? 2.f : 1.f;
    u[21] = (float)c.mask_pitch; u[22] = c.mask_strength; u[23] = c.mask_gain; u[24] = c.knee;
    ok = ok && Pass(Lines, &s_p.input, &s_p.lines, 0, 0, vw, h, u) && Pass(Beam, &s_p.lines, &s_p.beam, 0, 0, vw, vh, u);
    for (int i = 0; i < 2 && ok; i++) {
      Target *source = i ? &s_p.glow[0] : &s_p.beam, *a = i ? &s_p.halo[0] : &s_p.glow[0], *b = i ? &s_p.halo[1] : &s_p.glow[1];
      ok = ok && Pass(Down, source, a, 0, 0, a->w, a->h, u);
      u[12] = 1; u[13] = 0; ok = ok && Pass(Blur, a, b, 0, 0, b->w, b->h, u);
      u[12] = 0; u[13] = 1; ok = ok && Pass(Blur, b, a, 0, 0, a->w, a->h, u);
    }
    ok = ok && Pass(Compose, &s_p.beam, NULL, x, y, vw, vh, u);
  } else {
    if (settings->upscaler != kDkc1UpscalerReconstruct) u[4] = (float)settings->upscaler;
    ok = Pass(settings->upscaler == kDkc1UpscalerReconstruct ? Reconstruct : Flat, &s_p.input, NULL, x, y, vw, vh, u);
  }
  return ok;
}

static void ReadBackbuffer(uint8_t *bgra, int w, int h) {
  ID3D11Texture2D *back = NULL, *staging = NULL;
  if (FAILED(IDXGISwapChain1_GetBuffer(s_p.swapchain, 0, &IID_ID3D11Texture2D, (void **)&back))) return;
  D3D11_TEXTURE2D_DESC desc;
  ID3D11Texture2D_GetDesc(back, &desc);
  desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
  if (SUCCEEDED(ID3D11Device_CreateTexture2D(s_p.device, &desc, NULL, &staging))) {
    ID3D11DeviceContext_CopyResource(s_p.context, (ID3D11Resource *)staging, (ID3D11Resource *)back);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ID3D11DeviceContext_Map(s_p.context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped))) {
      for (int y = 0; y < h && y < (int)desc.Height; y++)
        memcpy(bgra + (size_t)y * w * 4, (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
               (size_t)(w < (int)desc.Width ? w : (int)desc.Width) * 4);
      ID3D11DeviceContext_Unmap(s_p.context, (ID3D11Resource *)staging, 0);
    }
  }
  RELEASE(staging); RELEASE(back);
}

static HRESULT PresentD3D(UINT sync_interval) {
  if (!s_p.d3d) return E_FAIL;
  HRESULT hr = IDXGISwapChain1_Present(s_p.swapchain, sync_interval, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    Fail("Present (device lost)", hr);
    s_p.d3d = 0;
  }
  return hr;
}

static int WaitForSlot(DWORD timeout_ms) {
  if (!s_p.d3d || !s_p.waitable) return 0;
  return WaitForSingleObjectEx(s_p.waitable, timeout_ms, FALSE) == WAIT_OBJECT_0;
}

typedef struct Scanout {
  UINT present_count, displayed_count, displayed_refresh, sync_refresh;
  LONGLONG sync_qpc;
  int disjoint, valid;
} Scanout;

static void ReadScanout(Scanout *out) {
  memset(out, 0, sizeof *out);
  if (!s_p.d3d) return;
  UINT count = 0;
  if (SUCCEEDED(IDXGISwapChain1_GetLastPresentCount(s_p.swapchain, &count))) out->present_count = count;
  DXGI_FRAME_STATISTICS stats;
  memset(&stats, 0, sizeof stats);
  HRESULT hr = IDXGISwapChain1_GetFrameStatistics(s_p.swapchain, &stats);
  if (hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) { out->disjoint = 1; return; }
  if (FAILED(hr)) return;
  out->displayed_count = stats.PresentCount; out->displayed_refresh = stats.PresentRefreshCount;
  out->sync_refresh = stats.SyncRefreshCount; out->sync_qpc = stats.SyncQPCTime.QuadPart; out->valid = 1;
}

/* Render and present one complete image through whichever backend is live.
 * Callers hold s_lock. */
static void RenderAndPresentLocked(const uint8_t *pixels, int w, int h, int display_width,
                                   const Dkc1GraphicsSettings *settings, UINT sync_interval) {
  if (!pixels || w < 1 || h < 1 || s_minimized) return;
  if (s_p.d3d) {
    if (RenderD3D(pixels, w, h, display_width, settings)) PresentD3D(sync_interval);
    return;
  }
  if (s_p.gl) {
    Dkc1WindowsGraphicsDraw((const uint32_t *)pixels, w, h, display_width, settings);
    Dkc1WindowsGraphicsSwap();
  }
}

/* ---- pacer ------------------------------------------------------------- */

typedef enum PaceMode { kPaceTimer = 0, kPaceWaitable, kPaceDwmFlush } PaceMode;
typedef HRESULT (WINAPI *DwmTimingFn)(HWND, DWM_TIMING_INFO *);
typedef HRESULT (WINAPI *DwmFlushFn)(void);

typedef struct Pacer {
  LARGE_INTEGER frequency;
  HANDLE timer;
  HMODULE dwm_module;
  DwmTimingFn dwm_timing;
  DwmFlushFn dwm_flush;
  PaceMode mode;
  int locked, divisor, minimized_pacing, timer_resolution_active, started;
  int mid_flushes; UINT mid_sync_interval;   /* worker copies */
  double refresh_hz, display_hz, period_ticks, half_period_ticks;
  double next_present_tick, wake_tick, last_wake_tick, last_submit_tick, last_present_tick, last_presented_tick;
  double work_start_tick, wait_start_tick, loop_start_tick;
  double pending_submit_tick, pending_submit_interval_ms, pending_submit_error_ms, pending_work_ms, pending_wait_ms;
  double pending_late_ms, pending_gap_ms, pending_wake_interval_ms, pending_stats_ms, pending_log_ms;
  int pending_wait_timeout, pending_mid_presented;
  double pending_mid_submit_tick, pending_mid_submit_error_ms, pending_mid_present_ms, pending_real_to_mid_ms;
  long pending_mid_after_frame;
  unsigned long long interp_steps_before, interp_bank_before[256];
  long tier_before;
  unsigned long overruns, frames, mid_skips;
  long test_stall_frame; DWORD test_stall_ms; int test_stall_fired;
  const char *clock_source, *priority_mode;
  HANDLE mmcss;
  FILE *log; HostPacingLog *async_log;
  Scanout scanout;
} Pacer;

static Pacer s_pacer;

/* frame generation state (declared early: the pacer consults it) */
static uint8_t s_mid_pixels[kMaxPixelBytes], s_smooth_pixels[kMaxPixelBytes];
static int s_smooth_valid, s_mid_valid;
static int s_fg_enabled, s_fg_supported, s_fg_force, s_fg_last_valid;
static Dkc1FrameGenStats s_fg_stats;

bool Dkc1WinFrameGenEnabled(void) { return s_fg_enabled != 0; }
bool Dkc1WinFrameGenSupported(void) { return s_fg_supported != 0; }
bool Dkc1WinFrameGenExtraRefresh(void) { return s_fg_enabled && (s_fg_supported || s_fg_force); }

static const char *ModeName(PaceMode mode) {
  switch (mode) {
    case kPaceWaitable: return "waitable";
    case kPaceDwmFlush: return "dwmflush";
    default: return "timer";
  }
}

static int OverrideHz(double *hz) {
  const char *text = getenv("DKC1_PRESENT_HZ");
  if (!text || !*text) return 0;
  char *end = NULL;
  const double parsed = strtod(text, &end);
  if (end && !*end && parsed >= 30.0 && parsed <= 240.0) { *hz = parsed; return 1; }
  return 0;
}

static double DisplayHz(Pacer *pacer) {
  DWM_TIMING_INFO timing;
  memset(&timing, 0, sizeof timing);
  timing.cbSize = sizeof timing;
  /* The desktop-wide query exposes the composition clock before this
   * window has accumulated per-window statistics. */
  if (pacer->dwm_timing && SUCCEEDED(pacer->dwm_timing(NULL, &timing)) && timing.qpcRefreshPeriod > 0) {
    pacer->clock_source = "dwm";
    if (timing.rateRefresh.uiDenominator > 0 && timing.rateRefresh.uiNumerator > 0)
      return (double)timing.rateRefresh.uiNumerator / (double)timing.rateRefresh.uiDenominator;
    return (double)pacer->frequency.QuadPart / (double)timing.qpcRefreshPeriod;
  }
  HDC dc = GetDC(s_p.window);
  const int hz = dc ? GetDeviceCaps(dc, VREFRESH) : 0;
  if (dc) ReleaseDC(s_p.window, dc);
  if (hz > 1) { pacer->clock_source = "display"; return (double)hz; }
  return 0.0;
}

/* Multimedia Class Scheduler "Games" characteristics keep the frame thread
 * from being descheduled through its present slot; EcoQoS opt-out keeps it
 * off efficiency cores. */
static const char *RaiseThreadPriority(HANDLE *mmcss) {
  HMODULE avrt = LoadLibraryA("avrt.dll");
  typedef HANDLE (WINAPI *AvSetFn)(LPCSTR, LPDWORD);
  AvSetFn av_set = avrt ? (AvSetFn)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA") : NULL;
  DWORD task_index = 0;
  HANDLE handle = av_set ? av_set("Games", &task_index) : NULL;
  if (mmcss) *mmcss = handle;
  if (handle) return "mmcss-games";
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  return "above-normal";
}

static void ReleaseThreadPriority(HANDLE mmcss) {
  if (!mmcss) return;
  HMODULE avrt = GetModuleHandleA("avrt.dll");
  typedef BOOL (WINAPI *AvRevertFn)(HANDLE);
  AvRevertFn av_revert = avrt ? (AvRevertFn)GetProcAddress(avrt, "AvRevertMmThreadCharacteristics") : NULL;
  if (av_revert) av_revert(mmcss);
}

static void DisablePowerThrottling(void) {
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
  PROCESS_POWER_THROTTLING_STATE state;
  memset(&state, 0, sizeof state);
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  typedef BOOL (WINAPI *SetInfoFn)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
  SetInfoFn set_info = kernel32 ? (SetInfoFn)GetProcAddress(kernel32, "SetProcessInformation") : NULL;
  if (set_info) set_info(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
#endif
}

static double Now(void) { LARGE_INTEGER now; QueryPerformanceCounter(&now); return (double)now.QuadPart; }

static void CancelMidPresent(void);

void Dkc1WinPacerStart(void) {
  Pacer *pacer = &s_pacer;
  memset(pacer, 0, sizeof *pacer);
  QueryPerformanceFrequency(&pacer->frequency);
  pacer->clock_source = "hardware";
  DisablePowerThrottling();
  pacer->priority_mode = RaiseThreadPriority(&pacer->mmcss);
  pacer->dwm_module = LoadLibraryA("dwmapi.dll");
  if (pacer->dwm_module) {
    pacer->dwm_timing = (DwmTimingFn)GetProcAddress(pacer->dwm_module, "DwmGetCompositionTimingInfo");
    pacer->dwm_flush = (DwmFlushFn)GetProcAddress(pacer->dwm_module, "DwmFlush");
  }
  if (OverrideHz(&pacer->refresh_hz)) {
    pacer->clock_source = "override";
  } else {
    pacer->display_hz = DisplayHz(pacer);
    if (pacer->display_hz > 0.0) {
      const int divisor = (int)(pacer->display_hz / 60.0 + 0.5);
      const double divided = divisor > 0 ? pacer->display_hz / (double)divisor : 0.0;
      /* An exact SNES cadence drifts through a 60 Hz compositor and shows a
       * periodic doubled/dropped frame; lock to the display when an integer
       * divisor lands within 59.5-60.5 Hz. */
      if (divided >= 59.5 && divided <= 60.5) { pacer->locked = 1; pacer->divisor = divisor; pacer->refresh_hz = divided; }
    }
    if (!pacer->locked) { pacer->refresh_hz = 60.098811862; pacer->clock_source = "hardware"; }
  }
  if (pacer->locked) pacer->mode = s_p.d3d ? kPaceWaitable : pacer->dwm_flush ? kPaceDwmFlush : kPaceTimer;
  else pacer->mode = kPaceTimer;
  {
    const char *framegen = getenv("DKC1_FRAMEGEN");
    if (framegen && *framegen) {
      s_fg_enabled = *framegen != '0';
      s_fg_force = _stricmp(framegen, "force") == 0;
    }
  }
  /* The 120 Hz pair needs the in-between image on its own refresh: an even
   * divisor gives it exactly half the emulated period. The worker renders
   * through Direct3D; the OpenGL fallback keeps 60 Hz pose smoothing only. */
  s_fg_supported = pacer->locked && pacer->divisor >= 2 && (pacer->divisor % 2) == 0 && s_p.d3d;
  if (s_fg_enabled && s_fg_force && !s_fg_supported) pacer->mode = kPaceTimer;
  Dkc1FrameGenSetEnabled(s_fg_enabled != 0);
  {
    const char *frame_text = getenv("DKC1_PACING_TEST_STALL_FRAME");
    const char *ms_text = getenv("DKC1_PACING_TEST_STALL_MS");
    if (frame_text && *frame_text && ms_text && *ms_text) {
      const long frame = strtol(frame_text, NULL, 10);
      const unsigned long ms = strtoul(ms_text, NULL, 10);
      if (frame > 0 && ms > 0 && ms <= 1000) { pacer->test_stall_frame = frame; pacer->test_stall_ms = (DWORD)ms; }
    }
  }
  pacer->period_ticks = (double)pacer->frequency.QuadPart / pacer->refresh_hz;
  pacer->half_period_ticks = pacer->period_ticks * 0.5;
  pacer->next_present_tick = pacer->wake_tick = pacer->work_start_tick = Now();
  pacer->timer = CreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!pacer->timer) {
    pacer->timer = CreateWaitableTimerA(NULL, FALSE, NULL);
    if (timeBeginPeriod(1) == TIMERR_NOERROR) pacer->timer_resolution_active = 1;
  }
  pacer->interp_steps_before = interp_bridge_steps_total();
  pacer->tier_before = interp_tier_hit_count();
  memcpy(pacer->interp_bank_before, interp_bridge_bank_steps(), sizeof pacer->interp_bank_before);
  const char *path = getenv("DKC1_PACING_LOG");
  if (path && *path) {
    pacer->log = fopen(path, "wb");
    if (pacer->log) {
      pacer->async_log = HostPacingLogOpen(pacer->log, path);
      if (!pacer->async_log) {
        fclose(pacer->log); pacer->log = NULL;
        fprintf(stderr, "[pacing] unable to create diagnostic queue\n");
      } else {
        HostPacingLogWrite(pacer->async_log,
          "{\"schema\":\"dkc1.pacing.v5\",\"host\":\"sdl\",\"refresh_hz\":%.9f,"
          "\"display_hz\":%.9f,\"clock_source\":\"%s\",\"pacing\":\"%s\","
          "\"presenter\":\"%s\",\"presenter_error\":\"%.120s\","
          "\"thread_priority\":\"%s\",\"submit_lead_ms\":0.0000,"
          "\"present_divisor\":%d,\"framegen\":%d,\"framegen_extra_refresh\":%d,"
          "\"framegen_half_period_ms\":%.4f,\"test_stall_frame\":%ld,"
          "\"test_stall_ms\":%lu,\"log_mode\":\"async\",\"log_capacity\":%u,"
          "\"max_frame_latency\":%u,\"pid\":%lu,\"main_tid\":%lu,\"qpc_frequency\":%lld}\n",
          pacer->refresh_hz, pacer->display_hz, pacer->clock_source, ModeName(pacer->mode),
          Dkc1WinPresenterName(), s_p.error, pacer->priority_mode, pacer->divisor,
          s_fg_enabled, Dkc1WinFrameGenExtraRefresh(),
          pacer->half_period_ticks * 1000.0 / (double)pacer->frequency.QuadPart,
          pacer->test_stall_frame, (unsigned long)pacer->test_stall_ms, kPacingLogSlots,
          s_p.max_frame_latency, GetCurrentProcessId(), GetCurrentThreadId(), pacer->frequency.QuadPart);
      }
    }
  }
  pacer->started = 1;
  fprintf(stderr, "[windows-present] presenter=%s pacing=%s display=%.3f Hz divisor=%d refresh=%.4f Hz framegen=%s%s\n",
          Dkc1WinPresenterName(), ModeName(pacer->mode), pacer->display_hz, pacer->divisor,
          pacer->refresh_hz, s_fg_enabled ? "on" : "off", s_fg_supported ? " (120 Hz pairs available)" : "");
}

bool Dkc1WinPacerWorkFirst(void) { return s_pacer.mode == kPaceTimer || Dkc1WinFrameGenExtraRefresh(); }
void Dkc1WinPacerMarkWorkStart(void) { s_pacer.work_start_tick = s_pacer.loop_start_tick = Now(); }
double Dkc1WinPacerRefreshHz(void) { return s_pacer.started ? s_pacer.refresh_hz : 60.0; }
const char *Dkc1WinPacerModeName(void) { return ModeName(s_pacer.mode); }
int Dkc1WinPacerDivisor(void) { return s_pacer.divisor; }
void Dkc1WinPacerSetMinimized(bool minimized) { s_minimized = minimized ? 1 : 0; }

static UINT SyncInterval(const Pacer *pacer, int paired) {
  if (pacer->mode != kPaceWaitable) return 0;
  return (UINT)HostPresentRefreshes(pacer->divisor, paired && Dkc1WinFrameGenExtraRefresh());
}

/* Pause/resume or a timeline change: drop any half-frame and restart the
 * timer schedule from now. The compositor-locked modes have no schedule. */
void Dkc1WinPacerReset(void) {
  CancelMidPresent();
  s_pacer.next_present_tick = Now();
  s_pacer.last_submit_tick = 0.0;
  s_pacer.last_present_tick = 0.0;
}

/* Block until the absolute tick `target`: high-resolution timer for the
 * coarse wait, then a bounded spin for the final millisecond. */
static void WaitUntil(Pacer *pacer, double target) {
  for (;;) {
    const double remaining = target - Now();
    if (remaining <= 0.0) break;
    const double remaining_ms = remaining * 1000.0 / (double)pacer->frequency.QuadPart;
    if (pacer->timer && remaining_ms > 1.25) {
      LARGE_INTEGER due;
      due.QuadPart = -(LONGLONG)((remaining_ms - 1.0) * 10000.0);
      if (!due.QuadPart) due.QuadPart = -1;
      if (SetWaitableTimer(pacer->timer, &due, 0, NULL, NULL, FALSE)) { WaitForSingleObject(pacer->timer, INFINITE); continue; }
    }
    YieldProcessor();
  }
}

void Dkc1WinPacerWaitFrame(bool work_first) {
  Pacer *pacer = &s_pacer;
  if (!pacer->started) return;
  const double before_wait = Now();
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  pacer->wait_start_tick = before_wait;
  pacer->pending_wait_timeout = 0;
  pacer->pending_late_ms = 0.0;
  if (work_first) pacer->pending_work_ms = (before_wait - pacer->work_start_tick) / ticks_per_ms;
  pacer->pending_gap_ms = pacer->last_presented_tick > 0.0
      ? (before_wait - pacer->last_presented_tick) / ticks_per_ms - (work_first ? pacer->pending_work_ms : 0.0) : 0.0;
  PaceMode mode = pacer->mode;
  if (mode == kPaceWaitable && (s_minimized || !s_p.d3d)) {
    /* A minimized flip-model window stops consuming presents before DXGI
     * throttles it; park on the timer instead of timing out. */
    if (!pacer->minimized_pacing) { pacer->minimized_pacing = 1; pacer->next_present_tick = before_wait; }
    mode = kPaceTimer;
  } else if (pacer->minimized_pacing) {
    pacer->minimized_pacing = 0;
  }
  switch (mode) {
    case kPaceWaitable:
      if (!WaitForSlot(250)) { pacer->pending_wait_timeout = 1; pacer->overruns++; }
      break;
    case kPaceDwmFlush: {
      /* The worker consumed half the divisor's passes when it presented the
       * midpoint; a frame without one waits for all of them. */
      const int passes = HostProducerFlushPasses(pacer->divisor, Dkc1WinFrameGenExtraRefresh() && pacer->pending_mid_presented);
      for (int i = 0; i < passes; i++) {
        if (!pacer->dwm_flush || FAILED(pacer->dwm_flush())) { pacer->pending_wait_timeout = 1; pacer->overruns++; break; }
      }
      break;
    }
    default: {
      double target = pacer->next_present_tick + pacer->period_ticks;
      if (before_wait > target) {
        /* Missed deadline: a complete interval from now, never a catch-up frame. */
        pacer->pending_late_ms = (before_wait - target) / ticks_per_ms;
        pacer->overruns++;
        target = before_wait;
      }
      pacer->next_present_tick = target;
      WaitUntil(pacer, target);
      break;
    }
  }
  pacer->wake_tick = Now();
  pacer->pending_wait_ms = (pacer->wake_tick - before_wait) / ticks_per_ms;
  pacer->pending_wake_interval_ms = pacer->last_wake_tick > 0.0 ? (pacer->wake_tick - pacer->last_wake_tick) / ticks_per_ms : 0.0;
  pacer->last_wake_tick = pacer->wake_tick;
  if (!work_first) pacer->work_start_tick = pacer->wake_tick;
}

static void BeginPresent(Pacer *pacer, int work_first) {
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  pacer->pending_submit_tick = Now();
  pacer->pending_submit_interval_ms = pacer->last_submit_tick > 0.0
      ? (pacer->pending_submit_tick - pacer->last_submit_tick) / ticks_per_ms : 0.0;
  pacer->pending_submit_error_ms = pacer->mode == kPaceTimer
      ? (pacer->pending_submit_tick - pacer->next_present_tick) / ticks_per_ms : 0.0;
  if (!work_first) {
    /* Wait-first: the budget runs from the slot signal to this present. */
    pacer->pending_work_ms = (pacer->pending_submit_tick - pacer->wake_tick) / ticks_per_ms;
    const double budget_ms = pacer->period_ticks / ticks_per_ms;
    if (pacer->pending_work_ms > budget_ms) { pacer->pending_late_ms = pacer->pending_work_ms - budget_ms; pacer->overruns++; }
  }
  pacer->last_submit_tick = pacer->pending_submit_tick;
}

static void Presented(Pacer *pacer, long host_frame, const Dkc1WinFrameTiming *timing) {
  const double present_tick = Now();
  const double ticks_per_ms = (double)pacer->frequency.QuadPart / 1000.0;
  const double interval_ms = pacer->last_present_tick > 0.0 ? (present_tick - pacer->last_present_tick) / ticks_per_ms : 0.0;
  const double present_ms = (present_tick - pacer->pending_submit_tick) / ticks_per_ms;
  pacer->last_present_tick = present_tick;
  pacer->frames++;
  if (pacer->log && s_p.d3d) ReadScanout(&pacer->scanout);
  else memset(&pacer->scanout, 0, sizeof pacer->scanout);
  pacer->pending_stats_ms = (Now() - present_tick) / ticks_per_ms;
  const unsigned long long steps_now = interp_bridge_steps_total();
  const long tier_now = interp_tier_hit_count();
  const unsigned long long *banks = interp_bridge_bank_steps();
  unsigned long long interp_steps = steps_now - pacer->interp_steps_before, bank_steps = 0;
  long tier_hits = tier_now - pacer->tier_before;
  unsigned interp_bank = 0;
  for (unsigned bank = 0; bank < 256; bank++) {
    const unsigned long long delta = banks[bank] - pacer->interp_bank_before[bank];
    if (delta > bank_steps) { bank_steps = delta; interp_bank = bank; }
  }
  pacer->interp_steps_before = steps_now;
  pacer->tier_before = tier_now;
  memcpy(pacer->interp_bank_before, banks, sizeof pacer->interp_bank_before);
  if (pacer->log) {
    static const Dkc1WinFrameTiming zero = {0};
    const Dkc1WinFrameTiming *t = timing ? timing : &zero;
    const Scanout *scan = &pacer->scanout;
    HostPacingLogWrite(pacer->async_log,
      "{\"frame\":%ld,\"work_ms\":%.4f,\"wait_ms\":%.4f,\"late_ms\":%.4f,"
      "\"present_interval_ms\":%.4f,\"submit_interval_ms\":%.4f,\"submit_error_ms\":%.4f,"
      "\"present_ms\":%.4f,\"setup_ms\":%.4f,\"emulation_ms\":%.4f,\"render_ms\":%.4f,"
      "\"diagnostics_ms\":%.4f,\"audio_ms\":%.4f,\"audio_queued_frames\":%d,"
      "\"audio_starvations\":%lu,\"audio_drops\":%lu,\"audio_ring_frames\":%lu,"
      "\"audio_internal_underflows\":%llu,\"framegen\":%d,\"interp_ms\":%.4f,"
      "\"interp_valid\":%d,\"interp_reject\":\"%s\",\"sprites_exact\":%u,"
      "\"sprites_actor\":%u,\"sprites_nearest\":%u,\"sprites_unmatched\":%u,"
      "\"actors_tracked\":%u,\"poses_changed\":%u,\"sprites_tween\":%u,\"tween_rects\":%u,"
      "\"tween_cells\":%u,\"tween_moved\":%u,\"tween_pixels\":%u,\"pose_actors\":%u,"
      "\"pose_pixels\":%u,\"pose_mismatch\":%u,\"pose_source_frame\":%d,"
      "\"max_scroll_step\":%d,\"mid_presented\":%d,\"mid_skips\":%lu,"
      "\"mid_submit_error_ms\":%.4f,\"mid_present_ms\":%.4f,\"mid_after_frame\":%ld,"
      "\"real_to_mid_ms\":%.4f,\"mid_to_real_ms\":%.4f,\"wait_timeout\":%d,"
      "\"present_count\":%u,\"stat_valid\":%d,\"stat_present_count\":%u,"
      "\"stat_present_refresh\":%u,\"stat_sync_refresh\":%u,\"stat_sync_qpc_ms\":%.4f,"
      "\"stat_disjoint\":%d,\"stat_lag_presents\":%d,\"pump_ms\":%.4f,\"slow_msg\":0,"
      "\"slow_msg_ms\":0.0000,\"stats_ms\":%.4f,\"gap_ms\":%.4f,\"wake_interval_ms\":%.4f,"
      "\"interp_steps\":%llu,\"tier_hits\":%ld,\"interp_bank\":%u,\"interp_bank_steps\":%llu,"
      "\"overruns\":%lu,\"submit_qpc_ms\":%.4f,\"present_end_qpc_ms\":%.4f,"
      "\"wait_start_qpc_ms\":%.4f,\"wake_qpc_ms\":%.4f,\"loop_start_qpc_ms\":%.4f,"
      "\"previous_log_ms\":%.4f,\"log_dropped\":%u,\"audio_mix_ms\":0.0000,\"audio_submit_ms\":0.0000}\n",
      host_frame, pacer->pending_work_ms, pacer->pending_wait_ms, pacer->pending_late_ms, interval_ms,
      pacer->pending_submit_interval_ms, pacer->pending_submit_error_ms, present_ms, t->setup_ms,
      t->emulation_ms, t->render_ms, t->diagnostics_ms, t->audio_ms, t->audio_queued_frames,
      t->audio_starvations, t->audio_drops, t->audio_ring_frames, t->audio_internal_underflows,
      s_fg_enabled, t->interp_ms, s_fg_last_valid, s_fg_stats.reject ? s_fg_stats.reject : "",
      s_fg_stats.sprites_exact, s_fg_stats.sprites_actor, s_fg_stats.sprites_nearest,
      s_fg_stats.sprites_unmatched, s_fg_stats.actors_tracked, s_fg_stats.poses_changed,
      s_fg_stats.sprites_tween, s_fg_stats.tween_rects, s_fg_stats.tween_cells, s_fg_stats.tween_moved,
      s_fg_stats.tween_pixels, s_fg_stats.pose_actors, s_fg_stats.pose_pixels, s_fg_stats.pose_mismatch,
      s_fg_stats.pose_source_frame, s_fg_stats.max_scroll_step, pacer->pending_mid_presented,
      pacer->mid_skips, pacer->pending_mid_submit_error_ms, pacer->pending_mid_present_ms,
      pacer->pending_mid_after_frame, pacer->pending_real_to_mid_ms,
      pacer->pending_mid_presented ? (pacer->pending_submit_tick - pacer->pending_mid_submit_tick) / ticks_per_ms : 0.0,
      pacer->pending_wait_timeout, scan->present_count, scan->valid, scan->displayed_count,
      scan->displayed_refresh, scan->sync_refresh, (double)scan->sync_qpc / ticks_per_ms, scan->disjoint,
      scan->valid ? (int)(scan->present_count - scan->displayed_count) : 0, t->events_ms,
      pacer->pending_stats_ms, pacer->pending_gap_ms, pacer->pending_wake_interval_ms, interp_steps,
      tier_hits, interp_bank, bank_steps, pacer->overruns, pacer->pending_submit_tick / ticks_per_ms,
      present_tick / ticks_per_ms, pacer->wait_start_tick / ticks_per_ms, pacer->wake_tick / ticks_per_ms,
      pacer->loop_start_tick / ticks_per_ms, pacer->pending_log_ms, pacer->async_log->dropped);
    pacer->pending_mid_submit_error_ms = pacer->pending_mid_present_ms = 0.0;
    pacer->pending_mid_presented = 0;
  }
  pacer->pending_log_ms = (Now() - present_tick) / ticks_per_ms - pacer->pending_stats_ms;
  pacer->last_presented_tick = present_tick;
}

static void PacerClose(void) {
  Pacer *pacer = &s_pacer;
  if (pacer->log) {
    HostPacingLogClose(pacer->async_log);
    pacer->async_log = NULL; pacer->log = NULL;
  }
  if (pacer->timer) CloseHandle(pacer->timer);
  if (pacer->timer_resolution_active) timeEndPeriod(1);
  if (pacer->dwm_module) FreeLibrary(pacer->dwm_module);
  ReleaseThreadPriority(pacer->mmcss);
  pacer->timer = NULL; pacer->dwm_module = NULL; pacer->mmcss = NULL; pacer->started = 0;
}

/* ---- half-frame worker (120 Hz frame generation) ------------------------ */

typedef struct MidPresenter {
  HANDLE thread, ready, done, cancel, stop, timer;
  PaceMode mode;
  LARGE_INTEGER frequency;
  DwmFlushFn dwm_flush;
  int mid_flushes; UINT sync_interval;
  double half_period_ticks, target, real_submit, submit, duration_ms;
  int width, height, presentation_width;
  Dkc1GraphicsSettings settings;
  uint8_t pixels[kMaxPixelBytes];
  long after_frame;
  int presented, skipped, pending, timed_out;
} MidPresenter;
static MidPresenter s_mid;

static int MidWait(MidPresenter *p) {
  HANDLE wait[2] = {p->cancel, NULL};
  switch (p->mode) {
    case kPaceWaitable:
      wait[1] = s_p.waitable;
      if (!wait[1]) return 0;
      switch (WaitForMultipleObjects(2, wait, FALSE, 250)) {
        case WAIT_OBJECT_0: return 0;
        case WAIT_OBJECT_0 + 1: return 1;
        default: p->timed_out = 1; return 1;
      }
    case kPaceDwmFlush:
      for (int i = 0; i < p->mid_flushes; i++) {
        if (WaitForSingleObject(p->cancel, 0) == WAIT_OBJECT_0) return 0;
        if (!p->dwm_flush || FAILED(p->dwm_flush())) { p->timed_out = 1; break; }
      }
      return WaitForSingleObject(p->cancel, 0) != WAIT_OBJECT_0;
    default:
      wait[1] = p->timer;
      for (;;) {
        if (WaitForSingleObject(p->cancel, 0) == WAIT_OBJECT_0) return 0;
        const double ms = (p->target - Now()) * 1000.0 / (double)p->frequency.QuadPart;
        if (ms <= 0.0) return 1;
        if (ms > 1.25 && wait[1]) {
          LARGE_INTEGER due;
          due.QuadPart = -(LONGLONG)((ms - 1.0) * 10000.0);
          if (SetWaitableTimer(wait[1], &due, 0, NULL, NULL, FALSE)) { WaitForMultipleObjects(2, wait, FALSE, INFINITE); continue; }
        }
        YieldProcessor();
      }
  }
}

static DWORD WINAPI MidThread(void *arg) {
  MidPresenter *p = (MidPresenter *)arg;
  HANDLE wake[2] = {p->stop, p->ready};
  HANDLE mmcss = NULL;
  RaiseThreadPriority(&mmcss);
  for (;;) {
    if (WaitForMultipleObjects(2, wake, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
    p->presented = p->skipped = p->timed_out = 0;
    p->submit = p->duration_ms = 0.0;
    const int slot = MidWait(p);
    AcquireSRWLockExclusive(&s_lock);
    const double now = Now();
    if (slot && WaitForSingleObject(p->cancel, 0) != WAIT_OBJECT_0) {
      /* Timer mode: never put a stale midpoint over the next real frame after
       * a stall; the compositor-locked slots are by definition the next one. */
      const int fresh = p->mode != kPaceTimer || now < p->target + p->half_period_ticks * .75;
      if (fresh && s_p.d3d) {
        p->submit = now;
        RenderAndPresentLocked(p->pixels, p->width, p->height, p->presentation_width, &p->settings, p->sync_interval);
        p->duration_ms = (Now() - p->submit) * 1000.0 / (double)p->frequency.QuadPart;
        p->presented = 1;
      }
    }
    p->skipped = !p->presented;
    ReleaseSRWLockExclusive(&s_lock);
    SetEvent(p->done);
  }
  ReleaseThreadPriority(mmcss);
  return 0;
}

static void CancelMidPresent(void) {
  MidPresenter *p = &s_mid;
  if (!p->thread) return;
  SetEvent(p->cancel);
  WaitForSingleObject(p->done, INFINITE);
  p->pending = 0;
}

static void CloseMidPresenter(void) {
  MidPresenter *p = &s_mid;
  CancelMidPresent();
  if (p->thread) { SetEvent(p->stop); WaitForSingleObject(p->thread, INFINITE); CloseHandle(p->thread); }
  if (p->ready) CloseHandle(p->ready);
  if (p->done) CloseHandle(p->done);
  if (p->cancel) CloseHandle(p->cancel);
  if (p->stop) CloseHandle(p->stop);
  if (p->timer) CloseHandle(p->timer);
  memset(p, 0, sizeof *p);
}

static int InitMidPresenter(void) {
  MidPresenter *p = &s_mid;
  if (p->thread) return 1;
  p->frequency = s_pacer.frequency;
  p->timer = CreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!p->timer) p->timer = CreateWaitableTimerA(NULL, FALSE, NULL);
  p->ready = CreateEventA(NULL, FALSE, FALSE, NULL);
  p->done = CreateEventA(NULL, TRUE, TRUE, NULL);
  p->cancel = CreateEventA(NULL, TRUE, FALSE, NULL);
  p->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (p->timer && p->ready && p->done && p->cancel && p->stop)
    p->thread = CreateThread(NULL, 0, MidThread, p, 0, NULL);
  if (!p->thread) { CloseMidPresenter(); return 0; }
  return 1;
}

/* Wait for the worker's half-frame, if any, and fold its telemetry into the
 * pacer. Runs before the main thread waits for its own slot so the midpoint
 * is always consumed first in compositor-locked modes. */
static void CollectMidPresent(Pacer *pacer) {
  MidPresenter *p = &s_mid;
  pacer->pending_mid_presented = 0;
  if (!p->pending) return;
  WaitForSingleObject(p->done, INFINITE);
  pacer->pending_mid_presented = p->presented;
  pacer->mid_skips += p->skipped;
  pacer->pending_mid_submit_tick = p->submit;
  pacer->pending_mid_after_frame = p->after_frame;
  pacer->pending_mid_submit_error_ms = p->presented && p->mode == kPaceTimer
      ? (p->submit - p->target) * 1000.0 / (double)pacer->frequency.QuadPart : 0.0;
  pacer->pending_mid_present_ms = p->duration_ms;
  pacer->pending_real_to_mid_ms = p->presented ? (p->submit - p->real_submit) * 1000.0 / (double)pacer->frequency.QuadPart : 0.0;
  if (p->timed_out) pacer->overruns++;
  p->pending = 0;
}

static void ScheduleMidPresent(Pacer *pacer, const uint8_t *pixels, int w, int h, int presentation_width,
                               const Dkc1GraphicsSettings *settings, long host_frame) {
  if (!InitMidPresenter()) { pacer->mid_skips++; return; }
  MidPresenter *p = &s_mid;
  p->width = w; p->height = h; p->presentation_width = presentation_width;
  p->settings = *settings;
  p->mode = pacer->mode;
  p->half_period_ticks = pacer->half_period_ticks;
  p->dwm_flush = pacer->dwm_flush;
  p->mid_flushes = pacer->divisor >= 2 ? pacer->divisor / 2 : 1;
  p->sync_interval = SyncInterval(pacer, 1);
  memcpy(p->pixels, pixels, (size_t)w * h * 4);
  p->target = pacer->last_submit_tick + pacer->half_period_ticks;
  p->real_submit = pacer->pending_submit_tick;
  p->after_frame = host_frame;
  p->pending = 1;
  ResetEvent(p->done);
  ResetEvent(p->cancel);
  SetEvent(p->ready);
}

/* ---- frame generation --------------------------------------------------- */

void Dkc1WinFrameGenSetEnabled(bool enabled) {
  CancelMidPresent();
  s_fg_enabled = enabled ? 1 : 0;
  s_smooth_valid = s_mid_valid = 0;
  Dkc1FrameGenSetEnabled(s_fg_enabled != 0);
  if (!s_fg_enabled) { s_fg_last_valid = 0; memset(&s_fg_stats, 0, sizeof s_fg_stats); }
}

void Dkc1WinFrameGenInvalidate(void) {
  CancelMidPresent();
  s_smooth_valid = s_mid_valid = s_fg_last_valid = 0;
  Dkc1FrameGenInvalidate();
}

void Dkc1WinFrameGenProcess(const uint8_t *raw, int width, int height, bool allowed) {
  s_smooth_valid = s_mid_valid = s_fg_last_valid = 0;
  memset(&s_fg_stats, 0, sizeof s_fg_stats);
  if (!s_fg_enabled || !allowed || !raw) return;
  const size_t pitch = (size_t)width * 4;
  int mid_valid = 0;
  if (Dkc1WinFrameGenExtraRefresh())
    mid_valid = Dkc1DrawInterpolatedFrame(s_mid_pixels, pitch, &s_fg_stats) ? 1 : 0;
  Dkc1DrawSmoothedFrames(raw, s_smooth_pixels, s_mid_pixels, mid_valid != 0, &s_fg_stats);
  (void)height;
  s_mid_valid = Dkc1WinFrameGenExtraRefresh();
  s_smooth_valid = 1;
  s_fg_last_valid = s_mid_valid;
}

const uint8_t *Dkc1WinFrameGenDisplay(const uint8_t *raw) { return s_smooth_valid ? s_smooth_pixels : raw; }
const uint8_t *Dkc1WinFrameGenMid(void) { return s_smooth_valid && s_mid_valid ? s_mid_pixels : NULL; }

/* ---- presenter lifecycle ------------------------------------------------ */

const char *Dkc1WinPresenterName(void) { return s_p.d3d ? "d3d11" : s_p.gl ? "opengl" : "none"; }
bool Dkc1WinPresenterIsD3D(void) { return s_p.d3d != 0; }

static void FallBackToOpenGl(void) {
  CancelMidPresent();
  char reason[sizeof s_p.error];
  memcpy(reason, s_p.error, sizeof reason);
  D3DShutdown();
  s_p.gl = Dkc1WindowsGraphicsInit(s_p.sdl) ? 1 : 0;
  s_fg_supported = 0;
  if (s_pacer.mode == kPaceWaitable) s_pacer.mode = s_pacer.dwm_flush ? kPaceDwmFlush : kPaceTimer;
  Dkc1WinPacerReset();
  fprintf(stderr, "[windows-present] Direct3D lost (%s); %s on %s pacing\n", reason,
          s_p.gl ? "OpenGL" : "no presenter", ModeName(s_pacer.mode));
}

bool Dkc1WinPresentInit(SDL_Window *window, char *error, size_t error_size) {
  memset(&s_p, 0, sizeof s_p);
  s_p.sdl = window;
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
    snprintf(error, error_size, "native window unavailable: %s", SDL_GetError());
    return false;
  }
  s_p.window = info.info.win.window;
  const char *presenter = getenv("DKC1_PRESENTER");
  const int want_gl = presenter && (_stricmp(presenter, "opengl") == 0 || _stricmp(presenter, "gl") == 0);
  if (!want_gl && D3DInit(s_p.window)) return true;
  if (!want_gl) fprintf(stderr, "[windows-present] Direct3D unavailable (%s); using OpenGL\n", s_p.error);
  s_p.gl = Dkc1WindowsGraphicsInit(window) ? 1 : 0;
  if (!s_p.gl) {
    snprintf(error, error_size, "no presenter: Direct3D %s; OpenGL %s", s_p.error[0] ? s_p.error : "declined", SDL_GetError());
    return false;
  }
  return true;
}

void Dkc1WinPresentClose(void) {
  CloseMidPresenter();
  PacerClose();
  D3DShutdown();
  if (s_p.gl) Dkc1WindowsGraphicsClose();
  memset(&s_p, 0, sizeof s_p);
}

void Dkc1WinPresentFrame(const uint8_t *display, const uint8_t *mid, int width, int height, int presentation_width,
                         const Dkc1GraphicsSettings *settings, bool paced, long host_frame,
                         const Dkc1WinFrameTiming *timing) {
  Pacer *pacer = &s_pacer;
  if (!paced || !pacer->started) {
    /* Menu or pause re-present: show immediately, keeping the compositor
     * cadence when locked; never leave a half-frame racing it. */
    CancelMidPresent();
    AcquireSRWLockExclusive(&s_lock);
    RenderAndPresentLocked(display, width, height, presentation_width, settings, SyncInterval(pacer, 0));
    ReleaseSRWLockExclusive(&s_lock);
    if (!s_p.d3d && !s_p.gl) FallBackToOpenGl();
    return;
  }
  const int work_first = Dkc1WinPacerWorkFirst();
  /* The half-frame worker must have presented before this thread waits for
   * its own slot: that fixes the order F, F+0.5, F+1. */
  CollectMidPresent(pacer);
  if (work_first) Dkc1WinPacerWaitFrame(true);
  BeginPresent(pacer, work_first);
  /* Only a frame that produced a midpoint splits its refreshes with it. */
  const int mid_follows = mid != NULL && Dkc1WinFrameGenExtraRefresh() && s_p.d3d;
  AcquireSRWLockExclusive(&s_lock);
  RenderAndPresentLocked(display, width, height, presentation_width, settings, SyncInterval(pacer, mid_follows));
  ReleaseSRWLockExclusive(&s_lock);
  Presented(pacer, host_frame, timing);
  if (!s_p.d3d && !s_p.gl) FallBackToOpenGl();
  else if (mid_follows) ScheduleMidPresent(pacer, mid, width, height, presentation_width, settings, host_frame);
  if (!pacer->test_stall_fired && pacer->test_stall_ms && host_frame == pacer->test_stall_frame) {
    pacer->test_stall_fired = 1;
    Sleep(pacer->test_stall_ms);
  }
}

/* ---- synthetic test ----------------------------------------------------- */

int Dkc1WinPresentTest(void) {
  SDL_Window *window = SDL_CreateWindow("Synthetic Direct3D test", 0, 0, 256, 224, SDL_WINDOW_HIDDEN);
  char error[256];
  if (!window || !Dkc1WinPresentInit(window, error, sizeof error)) { fprintf(stderr, "%s\n", error); return 1; }
  if (!s_p.d3d) { fprintf(stderr, "Direct3D presenter not selected: %s\n", s_p.error); return 10; }
  static uint32_t pixels[256 * 224], copy[256 * 224];
  static uint8_t out[256 * 224 * 4], repeat[256 * 224 * 4];
  for (int y = 0; y < 224; y++)
    for (int x = 0; x < 256; x++)
      pixels[y * 256 + x] = 0xff000000u | ((uint32_t)x << 16) | ((uint32_t)y << 8) | (((x + y) & 1) ? 80 : 180);
  memcpy(copy, pixels, sizeof copy);
  Dkc1GraphicsSettings s;
  Dkc1GraphicsDefault(&s);
  for (int mode = 0; mode < 12; mode++) {
    s.display = mode >= 9; s.upscaler = mode < 4 ? mode : 2; s.reconstruct_mode = mode < 4 ? 3 : (mode - 4) % 5;
    if (mode >= 9) Dkc1CrtSettingsApplyPreset(&s.crt, mode - 9);
    if (!RenderD3D((const uint8_t *)pixels, 256, 224, 256, &s)) return 2;
    ReadBackbuffer(out, 256, 224);
    if (mode == 0)
      for (size_t i = 0; i < 256 * 224; i++)
        if ((*(uint32_t *)(out + i * 4) & 0x00ffffffu) != (pixels[i] & 0x00ffffffu)) return 3;
    if (!RenderD3D((const uint8_t *)pixels, 256, 224, 256, &s)) return 2;
    ReadBackbuffer(repeat, 256, 224);
    if (memcmp(out, repeat, sizeof out) || memcmp(pixels, copy, sizeof copy)) return 4;
  }
  /* Real upscaling, including fractional geometry and same-size source
   * replacement; native-size checks alone can hide no-ops. */
  const int sizes[][2] = {{1024, 896}, {913, 701}};
  for (int size = 0; size < 2; size++) {
    const int width = sizes[size][0], height = sizes[size][1];
    SDL_SetWindowSize(window, width, height);
    const size_t bytes = (size_t)width * height * 4;
    uint8_t *base = malloc(bytes), *current = malloc(bytes), *again = malloc(bytes);
    if (!base || !current || !again) return 5;
    for (int mode = 0; mode < 12; mode++) {
      Dkc1GraphicsDefault(&s);
      s.display = mode >= 9; s.upscaler = mode < 4 ? mode : kDkc1UpscalerReconstruct; s.reconstruct_mode = mode < 4 ? 3 : (mode - 4) % 5;
      if (mode >= 9) Dkc1CrtSettingsApplyPreset(&s.crt, mode - 9);
      if (!RenderD3D((const uint8_t *)pixels, 256, 224, 256, &s)) return 6;
      if (s_p.width != width || s_p.height != height) return 11;
      ReadBackbuffer(current, width, height);
      if (!RenderD3D((const uint8_t *)pixels, 256, 224, 256, &s)) return 6;
      ReadBackbuffer(again, width, height);
      if (memcmp(current, again, bytes) || memcmp(pixels, copy, sizeof copy)) return 7;
      if (!mode) memcpy(base, current, bytes);
      else if (mode >= 9 && !memcmp(base, current, bytes)) return 8;  /* CRT must transform */
    }
    for (size_t i = 0; i < 256 * 224; i++) pixels[i] ^= 0x00ffffffu;
    if (!RenderD3D((const uint8_t *)pixels, 256, 224, 256, &s)) return 6;
    ReadBackbuffer(again, width, height);
    if (!memcmp(current, again, bytes)) return 9;
    memcpy(pixels, copy, sizeof copy);
    free(base); free(current); free(again);
  }
  Dkc1WinPresentClose();
  SDL_DestroyWindow(window);
  puts("WINDOWS_D3D11_PASS: flip-model swap chain; 12 modes at native, 4x and fractional sizes; exact native BGR; stable repeats; immutable input; source invalidation");
  return 0;
}
