#pragma once

#include <stdint.h>

#include "SD/sd_block_device.h"
#include "SD/sd_scheduler.h"
#include "Storage/generic_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_SAMPLE_RATE_HZ (48000U)
#define AUDIO_RECORDER_CHANNELS (2U)
#define AUDIO_RECORDER_BYTES_PER_FRAME (6U)
#define AUDIO_RECORDER_PATH_MAX (96U)

typedef enum
{
    AUDIO_RECORDER_CLIENT_NONE = 0,
    AUDIO_RECORDER_CLIENT_AUDIO_REC,
    AUDIO_RECORDER_CLIENT_LOOPER
} audio_recorder_client_t;

typedef enum
{
    AUDIO_RECORDER_STATE_IDLE = 0,
    AUDIO_RECORDER_STATE_PREPARED,
    AUDIO_RECORDER_STATE_RECORDING,
    AUDIO_RECORDER_STATE_DRAINING,
    AUDIO_RECORDER_STATE_FINALIZING,
    AUDIO_RECORDER_STATE_TAKE_READY,
    AUDIO_RECORDER_STATE_FAILED
} audio_recorder_state_t;

typedef enum
{
    AUDIO_RECORDER_ERROR_NONE = 0,
    AUDIO_RECORDER_ERROR_INVALID_ARGUMENT,
    AUDIO_RECORDER_ERROR_INVALID_STATE,
    AUDIO_RECORDER_ERROR_RING_OVERFLOW,
    AUDIO_RECORDER_ERROR_NO_SPACE,
    AUDIO_RECORDER_ERROR_SD_IO,
    AUDIO_RECORDER_ERROR_MEDIA_CHANGED
} audio_recorder_error_t;

typedef struct
{
    audio_recorder_state_t state;
    audio_recorder_error_t error;
    uint32_t frames_pending;
    uint32_t high_watermark;
    uint32_t overflow_count;
    uint32_t dropped_frames;
    uint32_t frames_received;
    uint32_t frames_assigned;
    uint32_t frames_committed;
} audio_recorder_status_t;

typedef struct
{
    generic_recorder_metrics_t recorder;
    sd_scheduler_metrics_t scheduler;
    recorder_file_reservation_metrics_t reservation;
    sd_block_device_async_metrics_t block_device;
    uint32_t release_duration_us;
    uint32_t header_duration_us;
    uint32_t sync_duration_us;
    uint32_t close_duration_us;
    uint32_t rename_duration_us;
    uint32_t finalization_duration_us;
    uint32_t superloop_iterations_during_write;
} audio_recorder_metrics_t;

typedef struct
{
    recorder_file_reservation_map_owned_t reservation;
    char path[AUDIO_RECORDER_PATH_MAX];
    uint32_t accepted_frames;
    uint32_t committed_frames;
} audio_recorder_live_stream_t;

void audio_recorder_init(void);
uint8_t audio_recorder_prepare(const char *temporary_rec_path,
                               const char *final_wav_path,
                               uint32_t frame_limit);
uint8_t audio_recorder_start(void);
uint8_t audio_recorder_push_from_irq(const int32_t *lr_interleaved,
                                     uint32_t frames);
uint8_t audio_recorder_request_stop(void);
void audio_recorder_service(void);
uint8_t audio_recorder_get_status(audio_recorder_status_t *status);
uint8_t audio_recorder_get_last_take(const char **path, uint32_t *frames);
void audio_recorder_get_metrics(audio_recorder_metrics_t *metrics);
uint8_t audio_recorder_is_active(void);
uint8_t audio_recorder_prepare_client(audio_recorder_client_t client,
                                      const char *temporary_rec_path,
                                      const char *final_wav_path,
                                      uint32_t frame_limit);
uint8_t audio_recorder_start_client(audio_recorder_client_t client);
uint8_t audio_recorder_push_from_irq_client(audio_recorder_client_t client,
                                            const int32_t *lr_interleaved,
                                            uint32_t frames);
uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client);
uint8_t audio_recorder_get_status_client(audio_recorder_client_t client,
                                         audio_recorder_status_t *status);
uint8_t audio_recorder_get_last_take_client(audio_recorder_client_t client,
                                            const char **path,
                                            uint32_t *frames);
uint8_t audio_recorder_get_live_stream(audio_recorder_client_t client,
                                       audio_recorder_live_stream_t *stream);
uint8_t audio_recorder_client_is_active(audio_recorder_client_t client);
uint8_t audio_recorder_client_is_recording(audio_recorder_client_t client);

/* AUDIO-side view: reads only the pointer-free M7<->M4 capture transport. */
uint8_t audio_recorder_capture_status_client(audio_recorder_client_t client,
                                             audio_recorder_status_t *status);

#ifdef __cplusplus
}
#endif
