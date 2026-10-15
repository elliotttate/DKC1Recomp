/* A second instance of the pinned renderer, with a private symbol namespace.
 * The co-op view owns a completely populated tilemap; it never consults or
 * mutates the primary view's streaming shadow or diagnostic evidence.
 * Keep the engine source read-only: reuse its rasterizer without a fork. */
#define PpuBeginDrawing Dkc1Private_PpuBeginDrawing
#define PpuBindOverlaySurface Dkc1Private_PpuBindOverlaySurface
#define PpuClearOverlayBindings Dkc1Private_PpuClearOverlayBindings
#define PpuClearOverlayCaptures Dkc1Private_PpuClearOverlayCaptures
#define PpuGetMode2Bg1Palette Dkc1Private_PpuGetMode2Bg1Palette
#define PpuGetMode2LayerCapture Dkc1Private_PpuGetMode2LayerCapture
#define PpuSetExtraSideSpace Dkc1Private_PpuSetExtraSideSpace
#define PpuSetExtraSpace Dkc1Private_PpuSetExtraSpace
#define PpuSetExtraSpaceCentered Dkc1Private_PpuSetExtraSpaceCentered
#define PpuSetMode2LayerCapture Dkc1Private_PpuSetMode2LayerCapture
#define PpuSetOverlayCapture Dkc1Private_PpuSetOverlayCapture
#define PpuSetOverlayOamRange Dkc1Private_PpuSetOverlayOamRange
#define PpuSetWidescreenBg3Widen Dkc1Private_PpuSetWidescreenBg3Widen
#define PpuSetWidescreenHudAlwaysVisible Dkc1Private_PpuSetWidescreenHudAlwaysVisible
#define PpuSetWidescreenHudSplit Dkc1Private_PpuSetWidescreenHudSplit
#define PpuSetWidescreenLayerAnchorBand Dkc1Private_PpuSetWidescreenLayerAnchorBand
#define PpuSetWidescreenLayerAnchorBandSlot Dkc1Private_PpuSetWidescreenLayerAnchorBandSlot
#define PpuSetWidescreenLayerClamp Dkc1Private_PpuSetWidescreenLayerClamp
#define PpuSetWidescreenLayerClampBand Dkc1Private_PpuSetWidescreenLayerClampBand
#define PpuSetWidescreenLayerMarginGap Dkc1Private_PpuSetWidescreenLayerMarginGap
#define PpuSetWidescreenLayerMask Dkc1Private_PpuSetWidescreenLayerMask
#define PpuSetWidescreenLayerMirror Dkc1Private_PpuSetWidescreenLayerMirror
#define PpuSetWidescreenLayerMirrorAxis Dkc1Private_PpuSetWidescreenLayerMirrorAxis
#define PpuSetWidescreenLayerRepeat Dkc1Private_PpuSetWidescreenLayerRepeat
#define PpuSetWidescreenLayerRepeatBand Dkc1Private_PpuSetWidescreenLayerRepeatBand
#define PpuSetWidescreenLayerStretchBand Dkc1Private_PpuSetWidescreenLayerStretchBand
#define PpuSetWidescreenLayerViewportInset Dkc1Private_PpuSetWidescreenLayerViewportInset
#define PpuSetWidescreenLineEnhancer Dkc1Private_PpuSetWidescreenLineEnhancer
#define PpuSetWidescreenPresentationXBias Dkc1Private_PpuSetWidescreenPresentationXBias
#define PpuSetWidescreenWindowExpansion Dkc1Private_PpuSetWidescreenWindowExpansion
#define PpuSetWsHudOamBand Dkc1Private_PpuSetWsHudOamBand
#define PpuSetWsHudOamShift Dkc1Private_PpuSetWsHudOamShift
#define PpuSetWsHudOamShiftRange Dkc1Private_PpuSetWsHudOamShiftRange
#define PpuSetWsHudOamShiftRange2 Dkc1Private_PpuSetWsHudOamShiftRange2
#define PpuWsExtraOverride Dkc1Private_PpuWsExtraOverride
#define PpuWsSetOamRightHints Dkc1Private_PpuWsSetOamRightHints
#define ppu_checkOverscan Dkc1Private_ppu_checkOverscan
#define ppu_free Dkc1Private_ppu_free
#define ppu_handleVblank Dkc1Private_ppu_handleVblank
#define ppu_init Dkc1Private_ppu_init
#define ppu_read Dkc1Private_ppu_read
#define ppu_reset Dkc1Private_ppu_reset
#define ppu_reset_internal_after_legacy_load Dkc1Private_ppu_reset_internal_after_legacy_load
#define ppu_runLine Dkc1Private_ppu_runLine
#define ppu_saveload Dkc1Private_ppu_saveload
#define ppu_saveload_internal Dkc1Private_ppu_saveload_internal
#define ppu_write Dkc1Private_ppu_write
#define g_snes_ppu_dbg_layer_mask s_coop_ppu_dbg_layer_mask
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include "debug_server.h"
#define WsShadowLayerActive(layer) false
#define WsShadowPixelBoundariesEnabled() false
#define WsShadowNativeLeft(layer) 0
#define WsShadowNativeRight(layer) 256
#define WsShadowWorldX(layer) 0u
#define WsShadowPresentWorldY(layer, x) 0u
#define WsShadowScrollY(layer) 0u
#define WsShadowTileDebug(layer, x, y, span, wy, hs, addr, real) (real)
#define WsShadowOnVramWrite(addr, value) ((void)0)
#define debug_server_on_ppu_line(line) ((void)0)
#define debug_server_on_oam_render() ((void)0)
#define debug_server_on_ppu_window(...) ((void)0)
#define debug_server_on_oam_write(...) ((void)0)
#define debug_server_on_vram_write(...) ((void)0)
extern bool Dkc1CoopViewEvaluateSprites(Ppu *ppu, int line);
extern uint16_t Dkc1CoopViewColor(Ppu *ppu, uint16_t pixel, unsigned x, unsigned y);
extern void Dkc1CoopViewWindow(Ppu*,int,int,int,int*,int*);
#include "../generated/host/dkc1_coop_ppu.inc"

/* Rasterize one materialized entity with the stock OBJ tile decoder. A host
 * priority/color surface replaces the cartridge's finite shared tile slots.
 * First OAM wins, including pixels whose BG priority is lower. */
void Dkc1Private_AccumulateSprites(Ppu *ppu, uint16_t *depth, uint16_t *colors) {
  ppu->renderFlags |= kPpuRenderFlags_NoSpriteLimits;
  for (int y = 0; y < 224; y++) {
    ClearBackdrop(&ppu->objBuffer);
    ppu_evaluateSprites(ppu, y);
    for (unsigned x = 0; x < kPpuBufWidth; x++) {
      uint16_t pixel = ppu->objBuffer.data[x];
      unsigned index = y * kPpuBufWidth + x;
      if ((pixel & 255u) && !(depth[index] & 255u)) {
        depth[index] = pixel;
        colors[index] = ppu->cgram[pixel & 255u];
      }
    }
  }
}
