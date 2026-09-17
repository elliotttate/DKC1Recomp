#include "desktop_graphics.h"
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void Dkc1GraphicsDefault(Dkc1GraphicsSettings *s) {
  *s = (Dkc1GraphicsSettings){.reconstruct_mode=3, .strength=100,
      .softness=50, .shading=60, .window_scale=3, .aspect=2, .edge=3,
      .audio_enabled=1, .volume=100};
  Dkc1CrtSettingsDefault(&s->crt);
}
void Dkc1GraphicsClamp(Dkc1GraphicsSettings *s) {
  s->display=Clamp(s->display,0,1); s->upscaler=Clamp(s->upscaler,0,3);
  s->screen=Clamp(s->screen,0,3); s->reconstruct_mode=Clamp(s->reconstruct_mode,0,4);
  s->strength=Clamp(s->strength,0,100); s->softness=Clamp(s->softness,0,100);
  s->shading=Clamp(s->shading,0,100); Dkc1CrtSettingsClamp(&s->crt);
  s->window_scale=Clamp(s->window_scale,1,4); s->fullscreen=!!s->fullscreen;
  s->aspect=Clamp(s->aspect,0,2); s->edge=Clamp(s->edge,0,3);
  s->audio_enabled=!!s->audio_enabled; s->volume=Clamp(s->volume,0,100);
  s->state_slot=Clamp(s->state_slot,0,4);
  s->hd_polish=Clamp(s->hd_polish,0,100);
  s->hd_finish=Clamp(s->hd_finish,0,100);
}
