/* Same-frame layer isolation captures from a saved native snapshot.
 *
 * Reloading the snapshot before every capture guarantees each image shows
 * the SAME emulated frame — the failure mode this replaces is manual F-key
 * captures that accidentally compare different frames. Runs out of process
 * so a live debugging session's PPU/HDMA state is never perturbed.
 *
 * usage: dkc1_layer_capture <rom.sfc> <snapshot.state> <output-dir>
 * DKC1_WIDESCREEN=0 selects the native 4:3 framebuffer (default wide).
 * Output: composite/bg1/bg2/bg3/obj .ppm (P6) plus layer_capture.json.
 */
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "verified_rom.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t s_pixels[kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4];

static int WritePpm(const char *path, const uint8_t *bgra, int width,
                    int height) {
  FILE *file = fopen(path, "wb");
  if (!file)
    return 0;
  fprintf(file, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const uint8_t *pixel = bgra + ((size_t)y * width + x) * 4;
      const uint8_t rgb[3] = {pixel[2], pixel[1], pixel[0]};
      if (fwrite(rgb, 1, 3, file) != 3) {
        fclose(file);
        return 0;
      }
    }
  }
  return fclose(file) == 0;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr,
            "usage: dkc1_layer_capture <rom.sfc> <snapshot.state> <outdir>\n");
    return 2;
  }
  const char *rom_path = argv[1];
  const char *snapshot_path = argv[2];
  const char *out_dir = argv[3];

  {
    const char *widescreen_text = getenv("DKC1_WIDESCREEN");
    Dkc1VideoSetWidescreen(!(widescreen_text && *widescreen_text == '0'));
  }
  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc1ReadVerifiedRom(rom_path, &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    fprintf(stderr, "%s: %s\n", rom_error, rom_path);
    return 2;
  }
  Dkc1VideoSetRom(rom, rom_size);
  RtlRegisterGame(Dkc1GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fprintf(stderr, "snesrecomp rejected the verified ROM\n");
    return 4;
  }
  const int width = Dkc1VideoWidth();
  const int height = kDkc1VideoHeight;
  Dkc1BeginDrawing(s_pixels, (size_t)width * 4);

  static const struct {
    const char *name;
    uint8_t mask;
  } kShots[] = {
      {"composite", 0xff}, {"bg1", 0x01}, {"bg2", 0x02},
      {"bg3", 0x04},       {"obj", 0x10},
  };

  int captured_frame = -1;
  for (size_t i = 0; i < sizeof kShots / sizeof kShots[0]; i++) {
    /* Reload before every shot: byte-identical starting state, so every
     * image is the exact same emulated frame under a different mask. */
    if (!RtlLoadSnapshot(snapshot_path)) {
      fprintf(stderr, "unable to load snapshot: %s\n", snapshot_path);
      return 5;
    }
    Dkc1DebugSetLayerMask(kShots[i].mask);
    if (!RtlRunFrame(0) && g_fail) {
      fprintf(stderr, "runtime failure while advancing one frame\n");
      return 6;
    }
    Dkc1DrawPpuFrame();
    if (captured_frame < 0)
      captured_frame = snes_frame_counter;
    else if (captured_frame != snes_frame_counter) {
      fprintf(stderr,
              "same-frame guarantee violated: %d vs %d — investigate\n",
              captured_frame, snes_frame_counter);
      return 7;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.ppm", out_dir, kShots[i].name);
    if (!WritePpm(path, s_pixels, width, height)) {
      fprintf(stderr, "unable to write %s\n", path);
      return 8;
    }
  }
  Dkc1DebugSetLayerMask(0xff);

  char meta[1200];
  snprintf(meta, sizeof meta, "%s/layer_capture.json", out_dir);
  FILE *file = fopen(meta, "wb");
  if (file) {
    fprintf(file,
            "{\"schema\":\"dkc1.layer-capture.v1\",\"snapshot\":\"%s\","
            "\"snes_frame\":%d,\"width\":%d,\"height\":%d,"
            "\"widescreen\":%s,"
            "\"images\":[\"composite.ppm\",\"bg1.ppm\",\"bg2.ppm\","
            "\"bg3.ppm\",\"obj.ppm\"]}\n",
            snapshot_path, captured_frame, width, height,
            Dkc1VideoIsWidescreen() ? "true" : "false");
    fclose(file);
  }
  printf("layer_capture_ok dir=%s frame=%d size=%dx%d\n", out_dir,
         captured_frame, width, height);
  free(rom);
  return 0;
}
