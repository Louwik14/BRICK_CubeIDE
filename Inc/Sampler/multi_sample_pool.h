#pragma once

#include <stdint.h>

#include "Sampler/sample_play_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MULTI_SAMPLE_MAX_SAMPLES          (512U)
#define MULTI_SAMPLE_POOL_MAX_INSTRUMENTS (32U)
#define MULTI_SAMPLE_POOL_MAX_SAMPLES     (MULTI_SAMPLE_MAX_SAMPLES)
#define MULTI_SAMPLE_POOL_MAX_ZONES       (2048U)
#define MULTI_SAMPLE_POOL_NAME_MAX        (32U)
#define MULTI_SAMPLE_POOL_PATH_MAX        (96U)
#define MULTI_SAMPLE_POOL_INVALID_ID      (0xFFFFU)

typedef enum
{
    MULTI_SAMPLE_INSTRUMENT_EMPTY = 0,
    MULTI_SAMPLE_INSTRUMENT_INDEXED,
    MULTI_SAMPLE_INSTRUMENT_LOADING,
    MULTI_SAMPLE_INSTRUMENT_READY,
    MULTI_SAMPLE_INSTRUMENT_ERROR
} multi_sample_instrument_state_t;

typedef struct
{
    uint16_t id;
    char name[MULTI_SAMPLE_POOL_NAME_MAX];
    char index_path[MULTI_SAMPLE_POOL_PATH_MAX];
    multi_sample_instrument_state_t state;
    uint8_t note_min;
    uint8_t note_max;
    uint16_t sample_count;
    uint16_t zone_count;
    uint16_t first_sample_id;
    uint16_t first_zone_id;
} multi_sample_instrument_t;

typedef struct
{
    uint16_t multi_sample_id;
    char path[MULTI_SAMPLE_POOL_PATH_MAX];
    uint32_t total_frames;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint8_t root_note;
    uint8_t vel_low;
    uint8_t vel_high;
    uint16_t flags;
} multi_sample_desc_t;

typedef struct
{
    uint8_t note_low;
    uint8_t note_high;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t root_note;
    uint16_t multi_sample_id;
} multi_sample_zone_t;

typedef struct
{
    uint16_t multi_sample_id;
    uint16_t zone_id;
    uint8_t root_note;
    int8_t pitch_semitones;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t velocity_layer_count_for_note;
    uint8_t zone_is_single_velocity_layer;
} multi_sample_resolve_result_t;

void multi_sample_pool_init(void);
void multi_sample_pool_reset(void);
uint16_t multi_sample_pool_get_instrument_count(void);
uint16_t multi_sample_pool_get_sample_capacity_used(void);
uint16_t multi_sample_pool_get_zone_capacity_used(void);
const multi_sample_instrument_t *multi_sample_pool_get_instrument(uint16_t instrument_id);
const multi_sample_desc_t *multi_sample_pool_get_sample(uint16_t multi_sample_id);
multi_sample_instrument_state_t multi_sample_pool_get_state(uint16_t instrument_id);
uint8_t multi_sample_pool_set_state(uint16_t instrument_id,
                                    multi_sample_instrument_state_t state);
uint8_t multi_sample_pool_set_index_path(uint16_t instrument_id, const char *path);
uint8_t multi_sample_pool_clear_instrument(uint16_t instrument_id);
uint8_t multi_sample_pool_resolve(uint16_t instrument_id,
                                  uint8_t note,
                                  uint8_t velocity,
                                  multi_sample_resolve_result_t *out_result);
uint8_t multi_sample_pool_resolve_source(uint16_t instrument_id,
                                         uint8_t note,
                                         uint8_t velocity,
                                         sample_resolved_source_t *out_source);

uint8_t multi_sample_pool_debug_define_instrument(uint16_t instrument_id,
                                                  const char *name,
                                                  multi_sample_instrument_state_t state);
uint8_t multi_sample_pool_debug_add_sample(uint16_t instrument_id,
                                           const char *path,
                                           uint32_t total_frames,
                                           uint8_t root_note,
                                           uint8_t vel_low,
                                           uint8_t vel_high,
                                           uint16_t flags,
                                           uint16_t *out_multi_sample_id);
uint8_t multi_sample_pool_set_sample_format(uint16_t multi_sample_id,
                                            uint32_t data_offset,
                                            uint32_t data_size,
                                            uint32_t sample_rate,
                                            uint16_t channels,
                                            uint16_t bits_per_sample);
uint8_t multi_sample_pool_debug_add_zone(uint16_t instrument_id,
                                         const multi_sample_zone_t *zone);

#ifdef __cplusplus
}
#endif
