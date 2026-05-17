#ifndef WAVEFORM_CACHE_H
#define WAVEFORM_CACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAVEFORM_CACHE_SAMPLE_ID_BYTES 16U
#define WAVEFORM_CACHE_TILE_COLUMNS 512U

typedef enum
{
    WAVEFORM_CACHE_STATE_INVALID = 0,
    WAVEFORM_CACHE_STATE_BUILDING = 1,
    WAVEFORM_CACHE_STATE_READY = 2
} waveform_cache_file_state_t;

typedef enum
{
    WAVEFORM_CACHE_LEVEL_L0_COARSE = 0,
    WAVEFORM_CACHE_LEVEL_L1_GLOBAL = 1,
    WAVEFORM_CACHE_LEVEL_L2_MID = 2,
    WAVEFORM_CACHE_LEVEL_L3_FINE = 3,
    WAVEFORM_CACHE_LEVEL_L4_VERY_FINE = 4,
    WAVEFORM_CACHE_LEVEL_COUNT
} waveform_cache_level_id_t;

typedef enum
{
    WAVEFORM_CACHE_REASON_EDITOR_VISIBLE = 0,
    WAVEFORM_CACHE_REASON_POST_AUDIO_REC = 1,
    WAVEFORM_CACHE_REASON_POST_LOOPER_SAVE = 2,
    WAVEFORM_CACHE_REASON_BACKGROUND = 3
} waveform_cache_reason_t;

typedef enum
{
    WAVEFORM_CACHE_STATUS_IDLE = 0,
    WAVEFORM_CACHE_STATUS_QUEUED,
    WAVEFORM_CACHE_STATUS_VALIDATING,
    WAVEFORM_CACHE_STATUS_BUILDING,
    WAVEFORM_CACHE_STATUS_READY,
    WAVEFORM_CACHE_STATUS_ERROR
} waveform_cache_status_t;

typedef struct
{
    waveform_cache_status_t status;
    uint8_t dirs_ready;
    uint8_t active_level_count;
    uint32_t frames_done;
    uint32_t frame_count;
    uint32_t last_fresult;
    uint32_t jobs_queued;
    uint32_t jobs_done;
    uint32_t jobs_failed;
} waveform_cache_diag_t;

typedef struct
{
    int16_t min;
    int16_t max;
} waveform_cache_minmax_t;

typedef struct
{
    uint8_t sample_id[WAVEFORM_CACHE_SAMPLE_ID_BYTES];
    uint32_t frame_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} waveform_cache_handle_t;

void waveform_cache_init(void);
uint8_t waveform_cache_ensure_dirs(void);
uint8_t waveform_cache_request_for_wav(const char *path, waveform_cache_reason_t reason);
void waveform_cache_service(uint32_t byte_budget);
void waveform_cache_get_diag(waveform_cache_diag_t *out_diag);

uint8_t waveform_cache_level_frames_per_column(waveform_cache_level_id_t level_id,
                                               uint32_t *out_frames_per_column);
uint8_t waveform_cache_choose_level(uint32_t frames_per_pixel,
                                    waveform_cache_level_id_t *out_level_id);
uint8_t waveform_cache_open_for_wav(const char *path, waveform_cache_handle_t *out_handle);
uint8_t waveform_cache_request_tiles(const waveform_cache_handle_t *handle,
                                     waveform_cache_level_id_t level_id,
                                     uint32_t tile_start,
                                     uint32_t tile_count,
                                     waveform_cache_reason_t reason);
uint8_t waveform_cache_tiles_ready(const waveform_cache_handle_t *handle,
                                   waveform_cache_level_id_t level_id,
                                   uint32_t tile_start,
                                   uint32_t tile_count);
uint8_t waveform_cache_minmax_from_ram(const waveform_cache_handle_t *handle,
                                       waveform_cache_level_id_t level_id,
                                       uint32_t column_start,
                                       uint32_t column_count,
                                       int16_t *out_min,
                                       int16_t *out_max);

#ifdef __cplusplus
}
#endif

#endif /* WAVEFORM_CACHE_H */
