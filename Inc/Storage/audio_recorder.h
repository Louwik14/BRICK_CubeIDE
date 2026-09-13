#pragma once

#include <stdint.h>

#include "SD/sd_block_device.h"
#include "SD/sd_scheduler.h"
#include "Storage/generic_recorder.h"
#include "Storage/audio_recorder_format.h"
#include "IPC/audio_recorder_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_PATH_MAX (96U)

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


typedef struct
{
    audio_recorder_state_t state;
    audio_recorder_error_t error;
    uint32_t frames_pending;
    uint32_t frames_received;
    uint32_t frames_assigned;
    uint32_t frames_committed;
} audio_recorder_status_t;

typedef enum
{
    AUDIO_RECORDER_LIFECYCLE_NOT_NOW = 0,
    AUDIO_RECORDER_LIFECYCLE_OK,
    AUDIO_RECORDER_LIFECYCLE_ERROR
} audio_recorder_lifecycle_result_t;

void audio_recorder_init(void);
void audio_recorder_service(void);
uint8_t audio_recorder_is_active(void);
uint8_t audio_recorder_prepare_client(audio_recorder_client_t client,
                                      const char *temporary_rec_path,
                                      const char *final_wav_path,
                                      uint32_t frame_limit);
audio_recorder_lifecycle_result_t audio_recorder_prepare_client_cooperative(
    audio_recorder_client_t client,
    const char *temporary_rec_path,
    const char *final_wav_path,
    uint32_t frame_limit);
uint8_t audio_recorder_start_client_at(audio_recorder_client_t client,
                                       uint64_t sample_time);
uint8_t audio_recorder_cancel_prepared_client(audio_recorder_client_t client);
audio_recorder_lifecycle_result_t audio_recorder_discard_client(
    audio_recorder_client_t client);
uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client);
uint8_t audio_recorder_request_stop_client_at(audio_recorder_client_t client,
                                              uint64_t sample_time);

uint8_t audio_recorder_get_status_client(audio_recorder_client_t client,
                                         audio_recorder_status_t *status);
uint8_t audio_recorder_get_last_take_client(audio_recorder_client_t client,
                                            const char **path,
                                            uint32_t *frames);
uint8_t audio_recorder_client_is_active(audio_recorder_client_t client);
uint8_t audio_recorder_client_is_recording(audio_recorder_client_t client);


#ifdef __cplusplus
}
#endif
