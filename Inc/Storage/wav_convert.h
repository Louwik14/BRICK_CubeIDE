#pragma once

#include <stdint.h>

#include "wav_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WAV_CONVERT_STATE_IDLE = 0,
    WAV_CONVERT_STATE_ACTIVE,
    WAV_CONVERT_STATE_DONE,
    WAV_CONVERT_STATE_FAILED
} wav_convert_state_t;

typedef enum
{
    WAV_CONVERT_ERROR_NONE = 0,
    WAV_CONVERT_ERROR_INVALID_ARG,
    WAV_CONVERT_ERROR_BUSY,
    WAV_CONVERT_ERROR_UNSUPPORTED,
    WAV_CONVERT_ERROR_MOUNT_FAIL,
    WAV_CONVERT_ERROR_OPEN_FAIL,
    WAV_CONVERT_ERROR_READ_FAIL,
    WAV_CONVERT_ERROR_WRITE_FAIL,
    WAV_CONVERT_ERROR_SYNC_FAIL,
    WAV_CONVERT_ERROR_CLOSE_FAIL,
    WAV_CONVERT_ERROR_VERIFY_FAIL,
    WAV_CONVERT_ERROR_REPLACE_FAIL,
    WAV_CONVERT_ERROR_NO_SPACE
} wav_convert_error_t;

uint8_t wav_convert_path_needs_48k(const char *path, wav_info_t *out_info);
uint8_t wav_convert_start_destructive_48k(const char *path);
void wav_convert_service(uint32_t byte_budget);
uint8_t wav_convert_is_active(void);
wav_convert_state_t wav_convert_get_state(void);
wav_convert_error_t wav_convert_get_last_error(void);
uint8_t wav_convert_get_progress_percent(void);
void wav_convert_clear_finished(void);

#ifdef __cplusplus
}
#endif
