#pragma once

#include <stdint.h>

#include "IPC/audio_recorder_capture.h"
#include "Storage/audio_recorder.h"
#include "Storage/recorder_file_reservation.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Local CM4 CONTROL -> STORAGE facade.  This is not an inter-core ABI:
 * paths, FatFs objects, callbacks, buffers and physical maps remain private
 * to the Storage implementation.  The only future inter-core data plane is
 * audio_recorder_capture_contract.h.
 */
typedef enum
{
    AUDIO_RECORDER_STORAGE_IDLE = 0,
    AUDIO_RECORDER_STORAGE_PREPARED,
    AUDIO_RECORDER_STORAGE_DRAINING,
    AUDIO_RECORDER_STORAGE_FINALIZING,
    AUDIO_RECORDER_STORAGE_TAKE_READY,
    AUDIO_RECORDER_STORAGE_FAILED
} audio_recorder_storage_phase_t;

/* Bounded value copy for the local Looper page-cache registration. */
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
uint8_t audio_recorder_storage_prepare(const char *temporary_rec_path,
                                       const char *final_wav_path);
uint8_t audio_recorder_storage_cancel(void);
void audio_recorder_storage_release(void);

/* Observe the existing capture transport, drain and advance the SD writer. */
void audio_recorder_storage_service(uint32_t session_id,
                                    uint8_t capture_is_active);

audio_recorder_storage_phase_t audio_recorder_storage_phase(void);
audio_recorder_error_t audio_recorder_storage_error(void);
void audio_recorder_storage_get_status(generic_recorder_status_t *status);
void audio_recorder_storage_get_metrics(audio_recorder_metrics_t *metrics);
uint64_t audio_recorder_storage_committed_tail(void);
uint8_t audio_recorder_storage_get_map_copy(
    audio_recorder_storage_map_copy_t *map);

#ifdef __cplusplus
}
#endif
