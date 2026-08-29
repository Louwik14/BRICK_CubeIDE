#pragma once

#include <stdint.h>

#include "Sampler/sample_classic_config.h"
#include "Storage/sd_access_gate.h"
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

typedef struct
{
    uint32_t preview_open_fail_count;
    uint32_t gate_release_on_error_count;
    char path[SAMPLE_CLASSIC_PATH_MAX];
    sd_access_client_t gate_owner;
    sd_access_client_t gate_last_owner;
    FRESULT fatfs_result;
} sd_preview_diag_t;

/*
 * Responsibility boundary:
 * - catalog selection stays in UI / wav_loader
 * - preview owns one exclusive SD session at a time
 * - import to the product Sample catalogue remains in sample_global_pool
 * - runtime sampler keeps consuming only canonical pool data
 */
void sd_preview_init(void);
uint8_t sd_preview_begin(const char *path);
uint8_t sd_preview_begin_range(const char *path, uint32_t start_frame, uint32_t end_frame);
void sd_preview_stop(void);
void sd_preview_process(void);
sd_preview_state_t sd_preview_get_state(void);
sd_preview_error_t sd_preview_get_last_error(void);
const sd_preview_diag_t *sd_preview_get_diag(void);
uint8_t sd_preview_is_active(void);
const char *sd_preview_get_path(void);
const wav_info_t *sd_preview_get_source_info(void);
void sd_preview_set_gain(float gain);
float sd_preview_get_gain(void);

/*
 * AUDIO-side MAIN insertion point. Consumes only the shared SPSC ring and its
 * pointer-free IPC metadata; it never reads the Storage decoder context.
 */
uint8_t sd_preview_render_main(float *out_main_l, float *out_main_r, uint32_t frames);
uint8_t sd_preview_audio_apply_active(uint8_t active, uint32_t epoch);
uint8_t sd_preview_audio_apply_gain(uint32_t gain_bits);

#ifdef __cplusplus
}
#endif
