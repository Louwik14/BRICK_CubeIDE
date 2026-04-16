#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SAMPLE_POOL_SIZE (64U)
#define SAMPLE_POOL_PATH_MAX (64U)

typedef enum
{
    SAMPLE_POOL_SLOT_EMPTY = 0,
    SAMPLE_POOL_SLOT_LOADED,
    SAMPLE_POOL_SLOT_MISSING
} sample_pool_slot_state_t;

typedef struct
{
    char path[SAMPLE_POOL_PATH_MAX];

    uint32_t data_offset;
    uint32_t length_frames;
    uint32_t bytes_per_frame;

    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;

    float *data;

    uint8_t valid;
} sample_desc_t;

/*
 * Phase 1 scope:
 * - metadata-only sample catalog for 64 project samples.
 * - no audio/DSP coupling.
 * - no runtime allocation.
 *
 * Samples are loaded fully in SDRAM and played directly from memory.
 */
void sample_pool_init(void);
bool sample_pool_load(uint16_t id, const char *path);
void sample_pool_clear(uint16_t id);
bool sample_pool_is_loaded(uint16_t id);
sample_pool_slot_state_t sample_pool_get_state(uint16_t id);
const sample_desc_t *sample_pool_get(uint16_t id);
