#pragma once

#include <stdint.h>

#include "Recorder/audio_recorder_ring.h"
#include "Storage/audio_recorder.h"
#include "Storage/recorder_file_reservation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local CONTROL -> STORAGE facade. Paths, FatFs objects, callbacks, buffers
 * and physical maps remain private to the Storage implementation. */
typedef enum
{
    AUDIO_RECORDER_STORAGE_IDLE = 0,
    AUDIO_RECORDER_STORAGE_PREPARING,
    AUDIO_RECORDER_STORAGE_PREPARED,
    AUDIO_RECORDER_STORAGE_DRAINING,
    AUDIO_RECORDER_STORAGE_FINALIZING,
    AUDIO_RECORDER_STORAGE_TAKE_READY,
    AUDIO_RECORDER_STORAGE_FAILED
} audio_recorder_storage_phase_t;

/* Bounded value copy for the local Recorder page-cache registration. */
typedef struct
{
    sample_stream_physical_extent_t extents[
        RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    uint64_t reserved_file_bytes;
    uint64_t valid_file_bytes;
    uint32_t media_epoch;
    uint16_t extent_count;
    uint16_t sector_size;
} audio_recorder_storage_map_copy_t;

void audio_recorder_storage_init(void);
audio_recorder_lifecycle_result_t audio_recorder_storage_prepare(
    const char *temporary_rec_path,
    const char *final_wav_path);
audio_recorder_lifecycle_result_t audio_recorder_storage_cancel(void);
void audio_recorder_storage_release(void);

/* Observe the existing capture transport, drain and advance the SD writer. */
void audio_recorder_storage_service(uint32_t session_id,
                                    uint8_t capture_is_active);

audio_recorder_storage_phase_t audio_recorder_storage_phase(void);
audio_recorder_error_t audio_recorder_storage_error(void);
uint64_t audio_recorder_storage_assigned_tail(void);
uint64_t audio_recorder_storage_committed_tail(void);
uint8_t audio_recorder_storage_get_map_copy(
    audio_recorder_storage_map_copy_t *map);

#ifdef __cplusplus
}
#endif
