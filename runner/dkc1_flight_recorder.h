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

/* Append a post-failure tail to an exported bundle: the inputs recorded
 * SINCE the export plus a final snapshot. A bundle whose anchor state
 * already contains the corruption still shows how the failure evolved,
 * and the frames after first detection are never lost. */
int Dkc1FlightRecorderExportTail(const char *bundle_dir, long from_frame,
                                 long to_frame, char *error,
                                 size_t error_size);

/* Host build identity recorded into exported bundle manifests. */
void Dkc1FlightRecorderSetBuildInfo(const char *build_info);

/* A native state load replaces the machine timeline.  Discard every input
 * and anchor from the old timeline, then capture the loaded state as the new
 * replay root at the unchanged host-frame number.  Without this, an export
 * after F12 can claim that pre-load inputs reproduce a completely different
 * machine state. */
int Dkc1FlightRecorderReanchorAfterStateLoad(long completed_frame,
                                             char *error,
                                             size_t error_size);
void Dkc1FlightRecorderRecord(long completed_frame, uint32_t input_mask);
int Dkc1FlightRecorderExport(long completed_frame,
                             char *bundle_path, size_t bundle_path_size,
                             char *error, size_t error_size);
void Dkc1FlightRecorderClose(void);

#endif
