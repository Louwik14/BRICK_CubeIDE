#pragma once

#include <stdint.h>

#include "Sampler/sample_global_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAVETABLE_POOL_MAX_SLOTS       (64U)
#define WAVETABLE_POOL_PATH_MAX        SAMPLE_GLOBAL_POOL_PATH_MAX
#define WAVETABLE_POOL_INVALID_SLOT    (0xFFFFU)
#define WAVETABLE_FRAME_SAMPLE_COUNT   (2048U)
#define WAVETABLE_POOL_FILE_MAGIC      (0x54573642UL) /* B6WT */
#define WAVETABLE_POOL_FILE_VERSION    (1U)
#define WAVETABLE_POOL_HEADER_SIZE     (32U)
#define WAVETABLE_PREVIEW_COLUMNS      (124U)

_Static_assert(WAVETABLE_POOL_MAX_SLOTS <= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
               "Wavetable slots must fit the active global audio asset pool");

typedef enum
{
    WAVETABLE_SLOT_EMPTY = 0,
    WAVETABLE_SLOT_LOADING,
    WAVETABLE_SLOT_READY,
    WAVETABLE_SLOT_ERROR
} wavetable_slot_state_t;

typedef enum
{
    WAVETABLE_FORMAT_NONE = 0,
    WAVETABLE_FORMAT_S16_MONO
} wavetable_format_t;

typedef enum
{
    WAVETABLE_FILE_SAMPLE_S16 = 1
} wavetable_file_sample_format_t;

typedef enum
{
    WAVETABLE_PREVIEW_EMPTY = 0,
    WAVETABLE_PREVIEW_READY,
    WAVETABLE_PREVIEW_ERROR
} wavetable_preview_state_t;

typedef enum
{
    WAVETABLE_RESULT_OK = 0,
    WAVETABLE_RESULT_INVALID_ARG,
    WAVETABLE_RESULT_POOL_FULL,
    WAVETABLE_RESULT_GLOBAL_SLOT_FULL,
    WAVETABLE_RESULT_GLOBAL_BUDGET_FULL,
    WAVETABLE_RESULT_RAM_POOL_FULL,
    WAVETABLE_RESULT_PATH_TOO_LONG,
    WAVETABLE_RESULT_SD_BUSY,
    WAVETABLE_RESULT_SD_MOUNT_FAIL,
    WAVETABLE_RESULT_OPEN_FAIL,
    WAVETABLE_RESULT_BAD_FILE,
    WAVETABLE_RESULT_UNSUPPORTED,
    WAVETABLE_RESULT_TOO_LARGE,
    WAVETABLE_RESULT_READ_FAIL,
    WAVETABLE_RESULT_REGISTER_FAIL
} wavetable_result_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t frame_sample_count;
    uint32_t frame_count;
    uint16_t sample_format;
    uint16_t reserved0;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t reserved1;
} wavetable_file_header_t;

typedef struct
{
    wavetable_preview_state_t state;
    uint32_t generation;
    uint32_t frame_count;
    uint16_t columns;
    uint16_t global_peak;
    int16_t min[WAVETABLE_PREVIEW_COLUMNS];
    int16_t max[WAVETABLE_PREVIEW_COLUMNS];
} wavetable_preview_t;

typedef struct
{
    wavetable_slot_state_t state;
    uint16_t global_slot;
    char path[WAVETABLE_POOL_PATH_MAX];
    wavetable_format_t format;
    uint32_t frame_sample_count;
    uint32_t frame_count;
    int16_t *data;
    uint32_t data_offset;
    uint16_t first_page_slot;
    uint16_t page_count;
    uint32_t generation;
    uint32_t data_bytes;
    uint32_t cost_bytes_aligned;
    wavetable_result_t error;
    uint32_t flags;
    wavetable_preview_t preview;
} wavetable_slot_t;

void wavetable_pool_init(void);
void wavetable_pool_reset(void);

uint16_t wavetable_pool_find_free_slot(void);
wavetable_result_t wavetable_pool_load_file(uint16_t wavetable_slot,
                                            const char *path,
                                            uint16_t *out_global_slot);
wavetable_result_t wavetable_pool_load_wav(uint16_t wavetable_slot,
                                           const char *path,
                                           uint16_t *out_global_slot);
wavetable_result_t wavetable_pool_load_file_at(uint16_t wavetable_slot,
                                               uint16_t global_slot,
                                               const char *path);
wavetable_result_t wavetable_pool_load_file_auto(const char *path,
                                                 uint16_t *out_wavetable_slot,
                                                 uint16_t *out_global_slot);
void wavetable_pool_clear(uint16_t wavetable_slot);

const wavetable_slot_t *wavetable_pool_get_slot(uint16_t wavetable_slot);
wavetable_slot_state_t wavetable_pool_get_state(uint16_t wavetable_slot);
const int16_t *wavetable_pool_get_data(uint16_t wavetable_slot);
const wavetable_preview_t *wavetable_pool_get_preview(uint16_t wavetable_slot);
const wavetable_preview_t *wavetable_pool_get_preview_for_global(uint16_t global_slot);
uint32_t wavetable_pool_get_used_bytes(void);
uint32_t wavetable_pool_get_free_bytes(void);
wavetable_result_t wavetable_pool_get_last_result(void);
const char *wavetable_pool_result_label(wavetable_result_t result);

#ifdef __cplusplus
}
#endif
