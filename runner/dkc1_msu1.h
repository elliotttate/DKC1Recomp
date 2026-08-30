#ifndef DKC1_MSU1_H
#define DKC1_MSU1_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc1Msu1 Dkc1Msu1;

/* Opens an extracted MSU-1 music directory containing track-N.pcm or
 * dkc_msu-N.pcm files. The returned object is host-only and never enters a
 * cartridge save state. */
Dkc1Msu1 *Dkc1Msu1Open(const char *directory, char *error,
                        size_t error_size);
void Dkc1Msu1Close(Dkc1Msu1 *player);

/* Applies only Conn's verified two-byte SPC music mute to an already checksum-
 * verified mutable ROM image. Sound effects continue through the stock SPC. */
int Dkc1Msu1ApplySpcMusicMute(uint8_t *rom, size_t rom_size,
                              char *error, size_t error_size);

/* Follow the stock music manager's requested ID ($0521) and start state
 * ($051D). Track number is theme+1, matching restoration MSU packs. */
void Dkc1Msu1ObserveMusicState(Dkc1Msu1 *player, uint16_t requested_theme,
                               uint16_t start_state);
void Dkc1Msu1Reset(Dkc1Msu1 *player);

/* Mixes 44.1-kHz signed stereo MSU PCM into the stock SPC output. */
void Dkc1Msu1Mix(Dkc1Msu1 *player, int16_t *samples, int frames,
                  int channels, int output_rate);

unsigned Dkc1Msu1CurrentTrack(const Dkc1Msu1 *player);
const char *Dkc1Msu1Directory(const Dkc1Msu1 *player);

#endif
