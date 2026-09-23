#ifndef SEQ_TIMING_H
#define SEQ_TIMING_H

#include <stdint.h>

#include "NoteFx/note_fx_event.h"

typedef enum
{
    SEQ_TIMING_BASE_1_4 = 0,
    SEQ_TIMING_BASE_1_8,
    SEQ_TIMING_BASE_1_8T,
    SEQ_TIMING_BASE_1_16,
    SEQ_TIMING_BASE_1_16T,
    SEQ_TIMING_BASE_1_32,
    SEQ_TIMING_BASE_COUNT
} seq_timing_base_t;

#define SEQ_GROOVE_NONE 0U
#define SEQ_GROOVE_COUNT 128U
#define SEQ_GROOVE_NAME_BYTES 64U
#define SEQ_GROOVE_FLAG_MISSING 0x01U
#define SEQ_GROOVE_MAX_PREPARED_NODES 512U
#define SEQ_TIMING_TRACK_COUNT 16U

typedef struct
{
    int32_t offset_q16[SEQ_GROOVE_MAX_PREPARED_NODES];
    uint32_t random_width_q16[SEQ_GROOVE_MAX_PREPARED_NODES];
    uint16_t velocity_q16[SEQ_GROOVE_MAX_PREPARED_NODES];
    uint64_t period_q32;
    uint64_t geometry_key;
    uint32_t base_period_q16;
    int32_t min_offset_q16;
    int32_t max_offset_q16;
    uint32_t max_random_width_q16;
    uint16_t node_count;
    uint8_t valid;
    uint8_t reserved;
} seq_groove_compiled_t;

typedef struct
{
    uint8_t base;
    uint8_t quantize;
    uint8_t groove;
    uint8_t timing;
    uint8_t random;
    int8_t velocity;
    uint8_t groove_flags;
    uint8_t global;
    char groove_name[SEQ_GROOVE_NAME_BYTES];
} seq_track_timing_config_t;

typedef struct
{
    uint32_t base_period_q16;
    uint32_t source_discovery_advance_q16;
    uint32_t max_quantize_advance_q16;
    uint32_t max_groove_timing_advance_q16;
    uint32_t max_random_advance_q16;
    uint32_t finalizer_max_advance_q16;
    uint32_t finalizer_max_delay_q16;
    uint32_t source_discovery_advance_samples;
    uint32_t finalizer_max_advance_samples;
    uint32_t finalizer_max_delay_samples;
    uint8_t quantize;
    uint8_t groove;
    uint8_t timing;
    uint8_t random;
    int8_t velocity;
    uint8_t global;
    uint8_t enabled;
    uint8_t track;
    const seq_groove_compiled_t *compiled;
    uint32_t groove_seed;
    uint32_t loop_period_q16;
    uint32_t reserved_plan;
} seq_track_timing_plan_t;

_Static_assert(sizeof(seq_track_timing_config_t) == 72U,
               "track timing configuration budget");
_Static_assert(sizeof(seq_groove_compiled_t) == 5160U,
               "compiled Groove block budget");
_Static_assert(sizeof(seq_track_timing_plan_t) == 64U,
               "compiled track timing plan budget");

void seq_timing_geometry_init(void);
uint8_t seq_timing_geometry_build_begin_with_workspace(
    const seq_track_timing_config_t config[SEQ_TIMING_TRACK_COUNT],
    seq_groove_compiled_t workspace[SEQ_TIMING_TRACK_COUNT]);
uint8_t seq_timing_geometry_build_begin(
    const seq_track_timing_config_t config[SEQ_TIMING_TRACK_COUNT]);
void seq_timing_geometry_build_abort(void);
void seq_timing_geometry_build_commit(
    seq_track_timing_plan_t plan[SEQ_TIMING_TRACK_COUNT]);
void seq_timing_compile(const seq_track_timing_config_t *config,
                        uint32_t samples_per_step_q16,
                        uint8_t track,
                        uint32_t groove_seed,
                        uint32_t loop_period_q16,
                        seq_track_timing_plan_t *plan);
int64_t seq_timing_quantize_position_q16(
    const seq_track_timing_plan_t *plan, int64_t position_q16);
uint64_t seq_timing_source_timestamp(
    const seq_track_timing_plan_t *plan, uint64_t nominal_sample,
    uint64_t transport_step_ordinal, int16_t microtiming,
    uint32_t samples_per_step_q16);
uint32_t seq_timing_advance_samples(uint32_t advance_q16,
                                    uint32_t samples_per_step_q16);
void seq_timing_finalize(const seq_track_timing_plan_t *plan,
                         uint64_t reference_sample,
                         uint64_t reference_position_q16,
                         uint32_t samples_per_step_q16,
                         note_event_t *event);

#endif
