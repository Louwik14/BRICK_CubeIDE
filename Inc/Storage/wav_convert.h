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

typedef enum
{
    WAV_CONVERT_PATH_INVALID = 0,
    WAV_CONVERT_PATH_CANONICAL,
    WAV_CONVERT_PATH_NEEDS_CANONICAL,
    WAV_CONVERT_PATH_BUSY
} wav_convert_path_status_t;

void wav_convert_init(void);
wav_convert_path_status_t wav_convert_path_canonical_status(
    const char *path, wav_info_t *out_info);
uint8_t wav_convert_path_needs_canonical(const char *path, wav_info_t *out_info);
uint8_t wav_convert_start_destructive_canonical(const char *path);
/* Project restore owns the closed mutation ingress while canonicalizing refs. */
uint8_t wav_convert_start_destructive_canonical_project(const char *path);
/* Caller owns the SD gate. Used by synchronous import domains such as Multi. */
uint8_t wav_convert_path_to_canonical_locked(const char *path);
void wav_convert_service(uint32_t byte_budget);
uint8_t wav_convert_is_active(void);
wav_convert_state_t wav_convert_get_state(void);
wav_convert_error_t wav_convert_get_last_error(void);
uint8_t wav_convert_get_progress_percent(void);
void wav_convert_clear_finished(void);

#ifdef __cplusplus
}
#endif
