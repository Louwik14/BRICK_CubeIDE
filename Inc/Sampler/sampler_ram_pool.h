#pragma once

#include <stdint.h>

#include "Sampler/sample_global_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLER_RAM_POOL_MAX_SLOTS      (16U)
#define SAMPLER_RAM_POOL_BYTES          (SAMPLE_PAGE_SLOT_POOL_COUNT * SAMPLE_PAGE_BYTES)
#define SAMPLER_RAM_POOL_PATH_MAX       SAMPLE_GLOBAL_POOL_PATH_MAX
#define SAMPLER_RAM_POOL_INVALID_SLOT   (0xFFFFU)

typedef enum
{
    SAMPLER_RAM_SLOT_EMPTY = 0,
    SAMPLER_RAM_SLOT_LOADING,
    SAMPLER_RAM_SLOT_READY,
    SAMPLER_RAM_SLOT_ERROR
} sampler_ram_slot_state_t;

typedef enum
{
    SAMPLER_RAM_FORMAT_NONE = 0,
    SAMPLER_RAM_FORMAT_FLOAT32_INTERLEAVED
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

typedef struct
{
    sampler_ram_slot_state_t state;
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
} sampler_ram_slot_t;

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
void sampler_ram_pool_clear(uint16_t ram_slot);

const sampler_ram_slot_t *sampler_ram_pool_get_slot(uint16_t ram_slot);
sampler_ram_slot_state_t sampler_ram_pool_get_state(uint16_t ram_slot);
const float *sampler_ram_pool_get_data(uint16_t ram_slot);
uint32_t sampler_ram_pool_get_cost(uint16_t ram_slot);
uint32_t sampler_ram_pool_get_used_bytes(void);
uint32_t sampler_ram_pool_get_free_bytes(void);
sampler_ram_result_t sampler_ram_pool_get_last_result(void);
const char *sampler_ram_pool_result_label(sampler_ram_result_t result);

#ifdef __cplusplus
}
#endif
