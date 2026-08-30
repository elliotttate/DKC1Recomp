#ifndef DKC1_HAPTICS_H
#define DKC1_HAPTICS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc1StompProbe {
  uint16_t actor_id;
  uint16_t actor_state;
  int16_t vertical_velocity;
  uint8_t player_slot;
  bool valid;
} Dkc1StompProbe;

/* Capture the pre-frame player state used to recognize the cartridge's
 * accepted enemy-stomp rebound after the frame completes. */
void Dkc1StompProbeCapture(Dkc1StompProbe *probe, const uint8_t *wram);

/* Returns true only for the BFA79C enemy-hit rebound. Normal jumps use $0700;
 * contact damage uses a different state/impulse and therefore fails closed. */
bool Dkc1StompProbeAccepted(const Dkc1StompProbe *probe,
                            const uint8_t *wram);

#endif
