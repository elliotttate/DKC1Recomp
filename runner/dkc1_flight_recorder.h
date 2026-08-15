#ifndef DKC1_FLIGHT_RECORDER_H
#define DKC1_FLIGHT_RECORDER_H

#include <stddef.h>
#include <stdint.h>

/* Rolling, default-off interactive repro recorder.
 *
 * DKC1_FLIGHT_RECORDER=1 keeps one minute of resolved joypad input and
 * periodic native snapshots in memory. DKC1_FLIGHT_RECORDER_DIR optionally
 * selects the export root (default: flight-recorder). Nothing is allocated,
 * hooked, or written when disabled.
 */
int Dkc1FlightRecorderInitialize(char *error, size_t error_size);
int Dkc1FlightRecorderEnabled(void);
void Dkc1FlightRecorderRecord(long completed_frame, uint32_t input_mask);
int Dkc1FlightRecorderExport(long completed_frame,
                             char *bundle_path, size_t bundle_path_size,
                             char *error, size_t error_size);
void Dkc1FlightRecorderClose(void);

#endif
