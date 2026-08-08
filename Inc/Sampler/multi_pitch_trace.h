#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_play_plan.h"
#include "Sampler/sample_voice_reader.h"

#ifndef BRICK6_MULTI_PITCH_TRACE
#define BRICK6_MULTI_PITCH_TRACE 0
#endif

#define BRICK6_MULTI_PITCH_TRACE_MAGIC       (0x4D505452UL) /* MPTR */
#define BRICK6_MULTI_PITCH_TRACE_ABI_VERSION (1U)
#define BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT (4096U)

typedef enum
{
    BRICK6_MULTI_PITCH_TRACE_EVENT_SEGMENT = 1U,
    BRICK6_MULTI_PITCH_TRACE_EVENT_VOICE = 2U,
    BRICK6_MULTI_PITCH_TRACE_EVENT_BLOCK = 3U
} brick6_multi_pitch_trace_event_kind_t;

enum
{
    BRICK6_MULTI_PITCH_TRACE_ANOM_PAGE_MISMATCH = (1U << 0),
    BRICK6_MULTI_PITCH_TRACE_ANOM_OFFSET_RANGE = (1U << 1),
    BRICK6_MULTI_PITCH_TRACE_ANOM_REF_INVALID = (1U << 2),
    BRICK6_MULTI_PITCH_TRACE_ANOM_EPOCH_INCOHERENT = (1U << 3),
    BRICK6_MULTI_PITCH_TRACE_ANOM_POSITION = (1U << 4),
    BRICK6_MULTI_PITCH_TRACE_ANOM_NAN_INF = (1U << 5),
    BRICK6_MULTI_PITCH_TRACE_ANOM_SOURCE_RANGE = (1U << 6)
};

typedef struct
{
    uint32_t magic;
    uint32_t abi_version;
    uint32_t header_size;
    uint32_t entry_size;
    uint32_t capacity;
    uint32_t write_index;
    uint32_t valid_count;
    uint32_t dropped_count;
    uint32_t reset_count;
    uint32_t block_sequence;
    uint32_t segment_count;
    uint32_t voice_count;
    uint32_t block_count;
    uint32_t anomaly_count;
    uint32_t last_block_cycles;
    uint32_t reserved;
} brick6_multi_pitch_trace_header_t;

/* Exactly 176 bytes. All fields are uint32_t so the raw dump has no ABI
 * dependency on compiler packing or DWARF. */
typedef struct
{
    uint32_t sequence;
    uint32_t event_kind;
    uint32_t block_sequence;
    uint32_t event_flags;
    uint32_t voice_id;
    uint32_t owner_track;
    uint32_t sample_id;
    uint32_t key_domain;
    uint32_t key_object_id;
    uint32_t voice_generation;
    uint32_t registration_epoch;
    uint32_t position_before_q16;
    uint32_t position_after_q16;
    uint32_t step_q16;
    uint32_t frames_rendered;
    uint32_t expected_page;
    uint32_t actual_page;
    uint32_t expected_neighbor_page;
    uint32_t actual_neighbor_page;
    uint32_t offset_frames;
    uint32_t current_slot;
    uint32_t neighbor_slot;
    uint32_t current_page_generation;
    uint32_t neighbor_page_generation;
    uint32_t current_epoch;
    uint32_t neighbor_epoch;
    uint32_t fraction_begin_q16;
    uint32_t fraction_end_q16;
    uint32_t loop_mode;
    uint32_t direction_before;
    uint32_t direction_after;
    uint32_t page_changed;
    uint32_t source_checksum;
    uint32_t source0_bits;
    uint32_t source1_bits;
    uint32_t neighbor_bits;
    uint32_t cycles;
    uint32_t scratch0;
    uint32_t scratch1;
    uint32_t scratch2;
    uint32_t scratch3;
    uint32_t multi_voice_count;
    uint32_t pitched_voice_count;
    uint32_t render_order;
} brick6_multi_pitch_trace_event_t;

#if BRICK6_MULTI_PITCH_TRACE
extern volatile brick6_multi_pitch_trace_header_t g_brick6_multi_pitch_trace_header;
extern volatile brick6_multi_pitch_trace_event_t
    g_brick6_multi_pitch_trace_ring[BRICK6_MULTI_PITCH_TRACE_ENTRY_COUNT];
#endif

void brick6_multi_pitch_trace_reset(void);
void brick6_multi_pitch_trace_block_begin(uint32_t frames,
                                          const void *scratch0,
                                          const void *scratch1,
                                          const void *scratch2,
                                          const void *scratch3);
void brick6_multi_pitch_trace_block_end(void);
uint32_t brick6_multi_pitch_trace_voice_begin(uint8_t voice_id,
                                              uint8_t owner_track,
                                              uint16_t sample_id,
                                              sample_audio_key_t key,
                                              uint32_t voice_generation,
                                              const sample_play_plan_t *plan);
void brick6_multi_pitch_trace_voice_end(uint32_t start_cycle,
                                        uint8_t voice_id,
                                        uint8_t owner_track,
                                        uint16_t sample_id,
                                        sample_audio_key_t key,
                                        uint32_t voice_generation,
                                        const void *scratch0,
                                        const void *scratch1);
void brick6_multi_pitch_trace_segment(uint8_t voice_id,
                                     uint8_t owner_track,
                                     uint16_t sample_id,
                                     sample_audio_key_t key,
                                     uint32_t voice_generation,
                                     const sample_play_plan_t *plan,
                                     const sample_audio_cursor_t *cursor_before,
                                     const sample_audio_segment_t *segment,
                                     float position_after,
                                     uint8_t direction_before,
                                     uint8_t direction_after);
