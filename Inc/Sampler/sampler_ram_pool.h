#pragma once

#include <stdint.h>
#include "Platform/brick_build_config.h"

#include "Sampler/sample_global_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLER_RAM_POOL_MAX_SLOTS      SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
#define SAMPLER_RAM_POOL_BYTES          (SAMPLE_PAGE_SLOT_POOL_COUNT * SAMPLE_PAGE_BYTES)
#define SAMPLER_RAM_POOL_PATH_MAX       SAMPLE_GLOBAL_POOL_PATH_MAX
#define SAMPLER_RAM_POOL_INVALID_SLOT   (0xFFFFU)
#define SAMPLE_RAM_WAVEFORM_MAX_COLUMNS (128U)
#define SAMPLE_RAM_WAVEFORM_COLUMNS     (124U)

typedef enum
{
    SAMPLER_RAM_SLOT_EMPTY = 0,
    SAMPLER_RAM_SLOT_LOADING,
    SAMPLER_RAM_SLOT_READY,
    SAMPLER_RAM_SLOT_RETIRING,
    SAMPLER_RAM_SLOT_ERROR
} sampler_ram_slot_state_t;

typedef enum
{
    SAMPLER_RAM_FORMAT_NONE = 0,
    SAMPLER_RAM_FORMAT_FLOAT32_MONO,
    SAMPLER_RAM_FORMAT_FLOAT32_STEREO_INTERLEAVED
} sampler_ram_format_t;

typedef enum
{
    SAMPLER_RAM_RESULT_OK = 0,
    SAMPLER_RAM_RESULT_INVALID_ARG,
    SAMPLER_RAM_RESULT_POOL_FULL,
    SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL,
    SAMPLER_RAM_RESULT_GLOBAL_BUDGET_FULL,
    SAMPLER_RAM_RESULT_RAM_POOL_FULL,
    SAMPLER_RAM_RESULT_PATH_TOO_LONG,
    SAMPLER_RAM_RESULT_SD_BUSY,
    SAMPLER_RAM_RESULT_SD_MOUNT_FAIL,
    SAMPLER_RAM_RESULT_OPEN_FAIL,
    SAMPLER_RAM_RESULT_WAV_PARSE_FAIL,
    SAMPLER_RAM_RESULT_WAV_UNSUPPORTED,
    SAMPLER_RAM_RESULT_TOO_LARGE,
    SAMPLER_RAM_RESULT_READ_FAIL,
    SAMPLER_RAM_RESULT_REGISTER_FAIL
} sampler_ram_result_t;

typedef enum
{
    SAMPLE_RAM_WAVEFORM_EMPTY = 0,
    SAMPLE_RAM_WAVEFORM_BUILDING,
    SAMPLE_RAM_WAVEFORM_READY,
    SAMPLE_RAM_WAVEFORM_ERROR
} sample_ram_waveform_state_t;

typedef struct
{
    sample_ram_waveform_state_t state;
    uint32_t sample_generation;
    uint32_t frame_count;
    uint8_t channels;
    uint8_t format;
    uint16_t columns;
    uint16_t ready_columns;
    uint16_t global_peak;
    uint32_t build_next_column;
    uint32_t build_next_frame;
    int16_t min[SAMPLE_RAM_WAVEFORM_MAX_COLUMNS];
    int16_t max[SAMPLE_RAM_WAVEFORM_MAX_COLUMNS];
} sample_ram_waveform_overview_t;

typedef struct
{
    volatile sampler_ram_slot_state_t state;
    uint16_t global_slot;
    char path[SAMPLER_RAM_POOL_PATH_MAX];
    sampler_ram_format_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t frames;
    uint16_t bytes_per_frame;
    float *data;
    uint32_t data_offset;
    uint16_t first_page_slot;
    uint16_t page_count;
    uint32_t generation;
    uint32_t data_bytes;
    uint32_t cost_bytes_aligned;
    sampler_ram_result_t error;
    uint32_t flags;
    sample_ram_waveform_overview_t waveform;
} sampler_ram_slot_t;

/* Physical RAM pages stay fixed at 16 KiB; the logical frame stride follows
 * the stored format. */
uint16_t sampler_ram_format_channels(sampler_ram_format_t format);
uint16_t sampler_ram_format_bytes_per_frame(sampler_ram_format_t format);
uint8_t sampler_ram_frames_to_bytes(sampler_ram_format_t format,
                                    uint32_t frames,
                                    uint32_t *out_bytes);
uint8_t sampler_ram_bytes_to_pages(uint32_t bytes, uint32_t *out_pages);
uint8_t sampler_ram_format_cost_bytes(sampler_ram_format_t format,
                                      uint32_t frames,
                                      uint32_t *out_logical_bytes,
                                      uint32_t *out_page_count,
                                      uint32_t *out_cost_bytes);

void sampler_ram_pool_init(void);
void sampler_ram_pool_reset(void);

uint16_t sampler_ram_pool_find_free_slot(void);
sampler_ram_result_t sampler_ram_pool_load_wav(uint16_t ram_slot,
                                               const char *path,
                                               uint16_t *out_global_slot);
sampler_ram_result_t sampler_ram_pool_load_wav_at(uint16_t ram_slot,
                                                  uint16_t global_slot,
                                                  const char *path);
sampler_ram_result_t sampler_ram_pool_load_wav_auto(const char *path,
                                                    uint16_t *out_ram_slot,
                                                    uint16_t *out_global_slot);
uint8_t sampler_ram_pool_load_async_begin(uint16_t ram_slot, const char *path);
void sampler_ram_pool_load_async_service(void);
uint8_t sampler_ram_pool_load_async_busy(void);
void sampler_ram_pool_load_async_cancel(void);
uint8_t sampler_ram_pool_load_async_take_result(sampler_ram_result_t *out_result,
                                                uint16_t *out_ram_slot,
                                                uint16_t *out_global_slot,
                                                const char **out_path);
void sampler_ram_pool_clear(uint16_t ram_slot);
void sampler_ram_pool_service_retire(void);

const sampler_ram_slot_t *sampler_ram_pool_get_slot(uint16_t ram_slot);
sampler_ram_slot_state_t sampler_ram_pool_get_state(uint16_t ram_slot);
const float *sampler_ram_pool_get_data(uint16_t ram_slot);
uint32_t sampler_ram_pool_get_cost(uint16_t ram_slot);
uint32_t sampler_ram_pool_get_used_bytes(void);
uint32_t sampler_ram_pool_get_free_bytes(void);
sampler_ram_result_t sampler_ram_pool_get_last_result(void);
const char *sampler_ram_pool_result_label(sampler_ram_result_t result);
void sampler_ram_pool_waveform_service(uint32_t frame_budget);
const sample_ram_waveform_overview_t *sampler_ram_pool_get_waveform(uint16_t ram_slot);
const sample_ram_waveform_overview_t *sampler_ram_pool_get_waveform_for_global(uint16_t global_slot);

#ifdef __cplusplus
}
#endif
