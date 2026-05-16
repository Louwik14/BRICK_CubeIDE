#pragma once

#include <stdint.h>

#include "wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SD_PREVIEW_STATE_IDLE = 0,
    SD_PREVIEW_STATE_OPENING,
    SD_PREVIEW_STATE_STREAMING,
    SD_PREVIEW_STATE_STOPPING,
    SD_PREVIEW_STATE_ERROR
} sd_preview_state_t;

typedef enum
{
    SD_PREVIEW_ERROR_NONE = 0,
    SD_PREVIEW_ERROR_INVALID_PATH,
    SD_PREVIEW_ERROR_BUSY,
    SD_PREVIEW_ERROR_GATE_REFUSED,
    SD_PREVIEW_ERROR_MOUNT_FAIL,
    SD_PREVIEW_ERROR_OPEN_FAIL,
    SD_PREVIEW_ERROR_PARSE_FAIL,
    SD_PREVIEW_ERROR_UNSUPPORTED_FORMAT,
    SD_PREVIEW_ERROR_READ_FAIL,
    SD_PREVIEW_ERROR_RECORD_ACTIVE
} sd_preview_error_t;

/*
 * Responsibility boundary:
 * - catalog selection stays in UI / wav_loader
 * - preview owns one exclusive SD session at a time
 * - import to project pool remains in sample_pool
 * - runtime sampler keeps consuming only canonical pool data
 */
void sd_preview_init(void);
uint8_t sd_preview_begin(const char *path);
uint8_t sd_preview_begin_range(const char *path, uint32_t start_frame, uint32_t end_frame);
void sd_preview_stop(void);
void sd_preview_process(void);
sd_preview_state_t sd_preview_get_state(void);
sd_preview_error_t sd_preview_get_last_error(void);
uint8_t sd_preview_is_active(void);
const char *sd_preview_get_path(void);
const wav_info_t *sd_preview_get_source_info(void);
void sd_preview_set_gain(float gain);
float sd_preview_get_gain(void);

/*
 * Future MAIN insertion point.
 * This is the stable contract for the audio runtime once the streaming
 * decoder/ring buffer lands. For this pass, the implementation remains a
 * no-op and returns 0 when no preview audio is available yet.
 */
uint8_t sd_preview_render_main(float *out_main_l, float *out_main_r, uint32_t frames);

#ifdef __cplusplus
}
#endif
