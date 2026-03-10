#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_RECORDER_MAX_STEMS 8U

typedef enum
{
    SD_RECORDER_STATE_IDLE = 0,
    SD_RECORDER_STATE_START_PENDING,
    SD_RECORDER_STATE_RECORDING,
    SD_RECORDER_STATE_STOP_PENDING,
    SD_RECORDER_STATE_FINALIZING,
    SD_RECORDER_STATE_ERROR
} sd_recorder_state_t;

typedef enum
{
    SD_RECORDER_TAP_TRACK_RAW = 0,
    SD_RECORDER_TAP_TRACK_POST_INSERT,
    SD_RECORDER_TAP_TRACK_POST_FADER,
    SD_RECORDER_TAP_TRACK_POST_SEND,
    SD_RECORDER_TAP_MASTER
} sd_recorder_tap_t;

typedef struct
{
    uint8_t bus_id;
    uint8_t channels;
    sd_recorder_tap_t tap;
    uint8_t reserved;
} sd_recorder_stem_cfg_t;

typedef struct
{
    sd_recorder_state_t state;
    uint32_t start_requests;
    uint32_t stop_requests;
    uint32_t arm_requests;
    uint32_t disarm_requests;
    uint32_t rejected_config_changes;
    uint32_t rejected_state_requests;
    uint32_t block_boundary_calls;
    uint32_t transition_count;
} sd_recorder_debug_t;

void sd_recorder_init(void);

uint8_t sd_recorder_request_start(void);
uint8_t sd_recorder_request_stop(void);
uint8_t sd_recorder_request_arm_stem(uint8_t stem_id,
                                     const sd_recorder_stem_cfg_t *cfg);
uint8_t sd_recorder_request_disarm_stem(uint8_t stem_id);

void sd_recorder_audio_block_begin(uint32_t frames);

sd_recorder_state_t sd_recorder_get_state(void);
void sd_recorder_get_debug(sd_recorder_debug_t *out_debug);

#ifdef __cplusplus
}
#endif
