#ifndef SEQ_RT_PASS1_H
#define SEQ_RT_PASS1_H

#include <stdint.h>
#include "Seq/seq_types.h"
#include "Seq/seq_model.h"
#include "NoteFx/note_fx_state.h"

#define SEQ_RT_BLOCK_FRAMES 64U
#define SEQ_RT_EVENT_CAPACITY 384U
#define SEQ_RT_BLOCK_SLOTS 3U
#define SEQ_RT_SNAPSHOT_SLOTS 2U
#define SEQ_RT_SOURCE_CAPACITY 192U
#define SEQ_RT_PENDING_CAPACITY 192U
#define SEQ_RT_LOCK_POOL_CAPACITY 384U
#define SEQ_RT_PARAM_EVENT_CAPACITY 256U
#define SEQ_RT_PARAM_FLAG_CLEARABLE UINT16_C(0x8000)
#define SEQ_RT_PARAM_FLAG_NOTE_FX UINT16_C(0x4000)
#define SEQ_RT_PARAM_ID_MASK UINT16_C(0x01FF)

typedef enum {
    SEQ_RT_EVENT_NOTE_OFF = 0,
    SEQ_RT_EVENT_PARAM,
    SEQ_RT_EVENT_NOTE_ON,
    SEQ_RT_EVENT_PANIC
} seq_rt_event_kind_t;

typedef struct {
    uint16_t offset;
    uint8_t kind;
    uint8_t track;
    uint32_t occurrence_id;
    int32_t value;
    uint8_t note;
    uint8_t velocity;
    uint16_t reserved;
} seq_rt_event_t;

typedef struct {
    uint64_t start_sample;
    uint32_t block_id;
    uint32_t generation;
    uint16_t comparable_tracks;
    uint16_t rt_note_tracks;
    uint16_t rt_plock_tracks;
    uint16_t event_count;
    uint16_t frames;
    seq_rt_event_t events[SEQ_RT_EVENT_CAPACITY];
} seq_rt_event_block_t;

typedef enum {
    SEQ_RT_PARAM_TEMP = 0,
    SEQ_RT_PARAM_CLEAR_TEMP,
    SEQ_RT_PARAM_RESTORE_BASE
} seq_rt_param_semantic_t;

typedef struct {
    uint16_t offset;
    uint16_t param_id;
    uint16_t value16;
    uint8_t track;
    uint8_t semantic;
} seq_rt_param_event_t;

typedef struct {
    uint64_t start_sample;
    uint32_t generation;
    uint16_t event_count;
    uint16_t frames;
    seq_rt_param_event_t events[SEQ_RT_PARAM_EVENT_CAPACITY];
} seq_rt_param_block_t;

typedef struct __attribute__((packed)) {
    uint16_t param_flags;
    uint16_t value16;
    uint16_t base_value16;
} seq_rt_lock_projection_t;

_Static_assert(sizeof(seq_rt_lock_projection_t) == 6U,
               "RT lock projection must remain compact");

typedef struct {
    uint16_t param_flags;
    uint16_t base_value16;
} seq_rt_active_lock_t;

typedef struct {
    uint64_t due_sample;
    uint32_t occurrence_id;
    uint8_t kind;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
} seq_rt_pending_event_t;

typedef struct {
    uint64_t first_on_sample;
    uint64_t span_q16;
    uint64_t interval_q16;
    uint64_t next_offset_q16;
    uint32_t gate_samples;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t active;
} seq_rt_note_source_t;

typedef struct {
    uint64_t step_sample_q16;
    uint32_t samples_per_step_q16;
    uint32_t transport_epoch;
    uint32_t projection_generation;
    uint32_t occurrence_serial;
    uint16_t source_count;
    uint16_t pending_count;
    uint32_t dropped_events;
    uint16_t comparable_tracks;
    uint16_t plock_fault_tracks;
    uint8_t running;
    uint8_t initialized;
    uint8_t event_faulted;
    uint8_t play_step[SEQ_LANE_CAPACITY];
    uint8_t track_div_phase[SEQ_LANE_CAPACITY];
    uint8_t track_swing_phase[SEQ_LANE_CAPACITY];
    uint32_t step_serial[SEQ_LANE_CAPACITY];
    uint32_t voice_scheduled_serial[SEQ_LANE_CAPACITY][SEQ_PLAY_MAX_CAPACITY];
    uint8_t active_lock_count[SEQ_LANE_CAPACITY];
    seq_rt_active_lock_t active_locks[SEQ_LANE_CAPACITY][SEQ_STEP_MAX_LOCKS];
    seq_rt_note_source_t sources[SEQ_RT_SOURCE_CAPACITY];
    seq_rt_pending_event_t pending[SEQ_RT_PENDING_CAPACITY];
} seq_rt_core_t;

/* CONTROL projection used by the shadow RT core. Lock payload/target
 * resolution is added before p-lock execution in the later migration pass. */
typedef struct {
    uint8_t trig_roll;
    uint8_t lock_count;
} seq_rt_step_projection_t;

typedef struct {
    uint32_t generation;
    uint64_t effective_sample;
    uint8_t track_length[SEQ_LANE_CAPACITY];
    uint8_t track_div[SEQ_LANE_CAPACITY];
    uint8_t track_swing[SEQ_LANE_CAPACITY];
    uint8_t track_quant[SEQ_LANE_CAPACITY];
    uint8_t track_muted[SEQ_LANE_CAPACITY];
    uint8_t track_can_emit[SEQ_LANE_CAPACITY];
    uint8_t track_rt_note_direct[SEQ_LANE_CAPACITY];
    uint8_t track_rt_plock_direct[SEQ_LANE_CAPACITY];
    uint8_t track_rt_fx_direct[SEQ_LANE_CAPACITY];
    uint8_t running;
    uint8_t scale_index;
    uint8_t root_index;
    uint8_t reserved0;
    uint32_t transport_epoch;
    uint64_t seed_step_sample_q16;
    uint32_t samples_per_step_q16;
    uint8_t seed_play_step[SEQ_LANE_CAPACITY];
    uint8_t seed_div_phase[SEQ_LANE_CAPACITY];
    uint8_t seed_swing_phase[SEQ_LANE_CAPACITY];
    seq_play_snapshot_t play_base[SEQ_LANE_CAPACITY];
    note_fx_track_state_t note_fx[SEQ_LANE_CAPACITY];
    seq_rt_step_projection_t steps[SEQ_LANE_CAPACITY][SEQ_MAX_STEPS];
    uint16_t lock_first[SEQ_LANE_CAPACITY][SEQ_MAX_STEPS];
    uint16_t lock_pool_count;
    seq_rt_lock_projection_t lock_pool[SEQ_RT_LOCK_POOL_CAPACITY];
    seq_play_snapshot_t top_play[BRICK_ENTITY_TOP_LEVEL_COUNT][SEQ_MAX_STEPS];
    seq_play_item_t child_play[BRICK_ENTITY_GROUP_CHILD_COUNT][SEQ_MAX_STEPS];
} seq_rt_projection_t;

typedef struct {
    uint32_t wakes;
    uint32_t ready;
    uint32_t missing;
    uint32_t late;
    uint32_t coalesced;
    uint32_t snapshot_generation;
    uint32_t max_irq_cycles;
    uint32_t last_block_id;
    uint32_t shadow_events;
    uint32_t legacy_events;
    uint32_t matched_events;
    uint32_t missing_shadow_events;
    uint32_t unexpected_legacy_events;
    uint32_t divergent_blocks;
    uint32_t dropped_events;
    uint32_t rt_audible_blocks;
    uint32_t rt_applied_events;
    uint32_t legacy_suppressed_events;
    uint64_t last_start_sample;
} seq_rt_pass1_diag_t;

/* Pure sample-domain shadow core. */
void seq_rt_core_init(seq_rt_core_t *core);
void seq_rt_core_process_block(seq_rt_core_t *core,
                               uint64_t start_sample, uint16_t frames,
                               const seq_rt_projection_t *projection,
                               seq_rt_event_block_t *out_block,
                               seq_rt_param_block_t *out_params);

/* CONTROL owns editable-model capture and publication. */
void seq_rt_pass1_control_init(void);
void seq_rt_pass1_control_mark_dirty(void);
void seq_rt_pass1_control_disarm_track(uint8_t track);
void seq_rt_pass1_control_poll(void);
const seq_rt_projection_t *seq_rt_pass1_projection_capture(void);

/* H743 adapter: AUDIO only checks the previous READY block and wakes SEQ. */
void seq_rt_pass1_irq_init(void);
void seq_rt_pass1_audio_boundary(uint64_t block_start_sample, uint8_t recovering);
void seq_rt_pass1_audio_observe_note(uint64_t sample, uint8_t kind,
                                     uint8_t track, uint8_t note,
                                     uint8_t velocity, uint32_t output_id);
uint16_t seq_rt_pass1_audio_frames_until_due(uint64_t sample,
                                             uint16_t maximum);
uint8_t seq_rt_pass1_audio_pop_due(uint64_t sample,
                                   seq_rt_event_t *out_event);
uint8_t seq_rt_pass1_audio_suppress_legacy(uint8_t kind, uint8_t track,
                                           uint32_t output_id);
uint8_t seq_rt_pass1_audio_suppress_legacy_param(uint8_t kind, uint8_t track);
void seq_rt_pass1_audio_retire_legacy(uint32_t output_id);
void seq_rt_pass1_audio_retire_occurrence(uint32_t occurrence_id);
uint16_t seq_rt_pass1_audio_track_mask(void);
void seq_rt_pass1_audio_force_stop(uint64_t effective_sample);
void seq_rt_pass1_get_diag(seq_rt_pass1_diag_t *out_diag);

#endif
