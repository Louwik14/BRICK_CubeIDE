#ifndef SEQ_ENGINE_H
#define SEQ_ENGINE_H

#include <stdint.h>
#include "Seq/seq_types.h"
#include "Seq/seq_model.h"
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_plan.h"
#include "Seq/seq_product_contract.h"
#include "Seq/seq_capacity_contract.h"

#define SEQ_ENGINE_H743_PERIOD_SAMPLES 64U
#define SEQ_ENGINE_TERMINAL_CAPACITY 3648U
#define SEQ_ENGINE_BLOCK_SLOTS 2U
#define SEQ_ENGINE_SNAPSHOT_SLOTS 2U
#define SEQ_ENGINE_LEDGER_CAPACITY 64U
#define SEQ_ENGINE_SOURCE_CAPACITY SEQ_PRODUCT_MAX_ACTIVE_SOURCES
#define SEQ_ENGINE_LOCK_POOL_CAPACITY 512U
#define SEQ_ENGINE_FX_SCRATCH_CAPACITY 32U
#define SEQ_ENGINE_INGRESS_CAPACITY 64U
#define SEQ_ENGINE_PARAM_FLAG_CLEARABLE UINT16_C(0x8000)
#define SEQ_ENGINE_PARAM_FLAG_NOTE_FX UINT16_C(0x4000)
#define SEQ_ENGINE_FX_PLAN_SLOT_MASK UINT16_C(0x0003)
#define SEQ_ENGINE_FX_PLAN_OVERRIDE_SHIFT 2U
#define SEQ_ENGINE_FX_PLAN_OVERRIDE_MASK UINT16_C(0x003C)
#define SEQ_ENGINE_PARAM_ID_MASK UINT16_C(0x01FF)

typedef enum {
    SEQ_ENGINE_EVENT_NOTE_OFF = 0,
    SEQ_ENGINE_EVENT_PARAM,
    SEQ_ENGINE_EVENT_NOTE_ON,
    SEQ_ENGINE_EVENT_PANIC
} seq_event_kind_t;

typedef enum {
    SEQ_ENGINE_PARAM_TEMP = 0,
    SEQ_ENGINE_PARAM_CLEAR_TEMP,
    SEQ_ENGINE_PARAM_RESTORE_BASE
} seq_param_semantic_t;

typedef union __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint32_t occurrence_id;
        uint16_t reserved;
        uint8_t track;
        uint8_t note;
        uint8_t velocity;
        uint8_t logical_slot;
    } note;
    struct __attribute__((packed)) {
        uint32_t value32;
        uint16_t param_id;
        uint8_t track;
        uint8_t semantic;
        uint8_t reserved[2];
    } param;
} seq_terminal_event_t;

_Static_assert(sizeof(seq_terminal_event_t) == 10U,
               "SEQ terminal payload budget");

#define SEQ_ENGINE_TERMINAL_INDEX_NONE UINT16_MAX
#define SEQ_ENGINE_TERMINAL_CLASS_COUNT 4U

typedef struct {
    uint64_t start_sample;
    uint64_t active_offsets;
    uint32_t block_id;
    uint32_t generation;
    uint16_t emitter_tracks;
    uint16_t lock_tracks;
    uint16_t event_count;
    uint16_t frames;
    uint16_t head[SEQ_ENGINE_H743_PERIOD_SAMPLES]
                 [SEQ_ENGINE_TERMINAL_CLASS_COUNT];
    uint16_t tail[SEQ_ENGINE_H743_PERIOD_SAMPLES]
                 [SEQ_ENGINE_TERMINAL_CLASS_COUNT];
    uint16_t next[SEQ_ENGINE_TERMINAL_CAPACITY];
    seq_terminal_event_t events[SEQ_ENGINE_TERMINAL_CAPACITY];
} seq_terminal_block_t;

typedef struct __attribute__((packed)) {
    uint16_t param_flags;
    uint16_t value16;
    uint16_t base_value16;
} seq_lock_pattern_t;

_Static_assert(sizeof(seq_lock_pattern_t) == 6U,
               "RT lock pattern must remain compact");

typedef struct {
    uint16_t param_flags;
    uint16_t value16;
    uint16_t base_value16;
} seq_active_lock_t;

typedef struct {
    uint64_t first_on_sample;
    uint64_t interval_q16;
    uint32_t gate_samples;
    uint32_t serial;
    uint8_t note;
    uint8_t velocity;
    uint8_t playback_stage;
    uint8_t next_ordinal;
    uint8_t ordinal_count;
    uint8_t active;
    uint8_t reserved[2];
} seq_source_cursor_t;

_Static_assert(sizeof(seq_source_cursor_t) == 32U,
               "SEQ source cursor budget");

typedef struct {
    uint64_t admitted_sample;
    uint64_t due_off;
    uint32_t occurrence_id;
    uint8_t note;
    uint8_t original;
    uint8_t reserved[2];
} seq_ledger_entry_t;

_Static_assert(sizeof(seq_ledger_entry_t) == 24U,
               "SEQ logical ledger budget");

typedef struct {
    uint64_t step_sample_q16;
    uint32_t samples_per_step_q16;
    uint32_t transport_epoch;
    uint32_t pattern_generation;
    uint32_t occurrence_serial;
    uint32_t transport_step_serial;
    uint16_t source_count;
    uint8_t ledger_count;
    uint8_t ledger_track_count[SEQ_LANE_CAPACITY];
    uint8_t logical_capacity[SEQ_LANE_CAPACITY];
    uint32_t dropped_events;
    uint16_t emitter_tracks;
    uint16_t plock_fault_tracks;
    uint8_t running;
    uint8_t initialized;
    uint8_t event_faulted;
    uint8_t play_step[SEQ_LANE_CAPACITY];
    uint8_t track_div_phase[SEQ_LANE_CAPACITY];
    uint8_t track_swing_phase[SEQ_LANE_CAPACITY];
    uint32_t step_serial[SEQ_LANE_CAPACITY];
    uint32_t voice_scheduled_serial[SEQ_LANE_CAPACITY][SEQ_PLAY_MAX_CAPACITY];
    note_fx_slot_plan_word_t fx_effective[SEQ_LANE_CAPACITY][NOTE_FX_SLOT_COUNT];
    uint8_t active_lock_count[SEQ_LANE_CAPACITY];
    seq_active_lock_t active_locks[SEQ_LANE_CAPACITY][SEQ_STEP_MAX_LOCKS];
    uint64_t source_active[SEQ_PRODUCT_MAX_SOURCE_GENERATIONS];
    uint64_t ledger_active;
    seq_source_cursor_t sources[SEQ_PRODUCT_MAX_SOURCE_GENERATIONS]
                               [SEQ_PRODUCT_MAX_EMITTING_VOICES];
    seq_ledger_entry_t ledger[SEQ_ENGINE_LEDGER_CAPACITY];
} seq_engine_core_t;

/* Immutable canonical Pattern armed by CONTROL and owned by SEQ after commit. */
typedef struct __attribute__((packed)) {
    uint8_t trig_roll;
    uint8_t lock_count;
} seq_step_pattern_t;

_Static_assert(sizeof(seq_step_pattern_t) == 2U,
               "compact executable step layout changed");

typedef struct {
    uint16_t capabilities;
    uint16_t max_negative_horizon_q16;
    uint8_t logical_capacity;
    uint8_t role;
    uint8_t type;
    uint8_t destination;
    uint8_t div;
    uint8_t swing;
    uint8_t quant;
    uint8_t muted;
    uint8_t active;
} seq_track_exec_t;

typedef struct {
    uint32_t generation;
    uint64_t effective_sample;
    uint8_t track_length[SEQ_LANE_CAPACITY];
    uint8_t track_div[SEQ_LANE_CAPACITY];
    uint8_t track_swing[SEQ_LANE_CAPACITY];
    uint8_t track_quant[SEQ_LANE_CAPACITY];
    uint8_t track_muted[SEQ_LANE_CAPACITY];
    uint8_t track_can_emit[SEQ_LANE_CAPACITY];
    uint8_t track_note_enabled[SEQ_LANE_CAPACITY];
    uint8_t track_lock_enabled[SEQ_LANE_CAPACITY];
    uint8_t track_fx_enabled[SEQ_LANE_CAPACITY];
    uint8_t running;
    uint8_t scale_index;
    uint8_t root_index;
    uint8_t reserved0;
    uint32_t transport_epoch;
    uint64_t seed_step_sample_q16;
    uint32_t samples_per_step_q16;
    seq_track_exec_t track_exec[SEQ_LANE_CAPACITY];
    note_fx_compiled_plan_t fx_base_plan[SEQ_LANE_CAPACITY];
    uint8_t seed_play_step[SEQ_LANE_CAPACITY];
    uint8_t seed_div_phase[SEQ_LANE_CAPACITY];
    uint8_t seed_swing_phase[SEQ_LANE_CAPACITY];
    seq_play_snapshot_t play_base[SEQ_LANE_CAPACITY];
    seq_step_pattern_t steps[SEQ_LANE_CAPACITY][SEQ_MAX_STEPS];
    uint16_t lock_first[SEQ_LANE_CAPACITY][SEQ_MAX_STEPS];
    uint16_t lock_pool_count[SEQ_LANE_CAPACITY];
    seq_lock_pattern_t *lock_pool[SEQ_LANE_CAPACITY];
    seq_play_snapshot_t top_play[BRICK_ENTITY_TOP_LEVEL_COUNT][SEQ_MAX_STEPS];
    seq_play_item_t child_play[BRICK_ENTITY_GROUP_CHILD_COUNT][SEQ_MAX_STEPS];
} seq_pattern_t;

/* CPU-agnostic absolute-sample core. */
void seq_engine_core_init(seq_engine_core_t *core);
void seq_engine_core_process_block(seq_engine_core_t *core,
                               uint64_t start_sample, uint16_t frames,
                               const seq_pattern_t *pattern,
                               seq_terminal_block_t *out_block);
uint8_t seq_engine_core_submit_live(seq_engine_core_t *core,
                                    const note_event_t *event,
                                    const seq_pattern_t *pattern,
                                    uint64_t window_start,
                                    uint64_t window_end,
                                    seq_terminal_block_t *out_block);

void seq_service(uint64_t now_sample, uint64_t publish_until_sample);
uint64_t seq_next_deadline(void);
uint8_t seq_engine_playhead_view(uint8_t track, uint8_t *out_running,
                                 uint8_t *out_step);
typedef struct {
    uint64_t capture_sample;
    uint32_t occurrence_id;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t kind;
    uint8_t provenance;
} seq_ingress_event_t;
uint8_t seq_ingress_submit(const seq_ingress_event_t *event);
void seq_ingress_discard(void);
void seq_ingress_panic(void);

/* CONTROL prepares; SEQ atomically takes ownership of the armed Pattern. */
void seq_engine_control_init(void);
void seq_engine_control_mark_dirty(void);
void seq_engine_control_disarm_track(uint8_t track);
void seq_engine_control_poll(void);
const seq_pattern_t *seq_engine_pattern_capture(void);

/* H743 adapter: AUDIO only checks the previous READY block and wakes SEQ. */
void seq_engine_irq_init(void);
void seq_engine_audio_boundary(uint64_t block_start_sample, uint8_t recovering);
uint16_t seq_engine_audio_frames_until_due(uint64_t sample,
                                             uint16_t maximum);
uint8_t seq_engine_audio_pop_due(uint64_t sample,
                                   uint8_t *out_kind,
                                   seq_terminal_event_t *out_event);
void seq_engine_audio_retire_occurrence(uint32_t occurrence_id);
uint16_t seq_engine_audio_track_mask(void);
void seq_engine_audio_force_stop(uint64_t effective_sample,
                                 uint8_t preserve_live_notes);

_Static_assert(SEQ_LANE_CAPACITY == 16U, "SEQ requires 16 lanes");
_Static_assert(SEQ_PLAY_MAX_CAPACITY == 8U, "SEQ requires 8 PLAY per top lane");
_Static_assert(SEQ_STEP_MAX_LOCKS == 32U, "SEQ requires 32 locks per step");
_Static_assert(NOTE_FX_SLOT_COUNT == 4U, "SEQ requires four MIDI FX slots");
_Static_assert(SEQ_ENGINE_INGRESS_CAPACITY
                   == SEQ_INGRESS_EVENTS_PER_WINDOW_MAX,
               "inbox and raw ingress rate contracts diverged");
_Static_assert(SEQ_ENGINE_LEDGER_CAPACITY == 64U, "SEQ logical ledger contract");
_Static_assert(SEQ_ENGINE_SOURCE_CAPACITY == 192U, "SEQ source cursor contract");
_Static_assert(SEQ_ENGINE_TERMINAL_CAPACITY >= SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON,
               "SEQ terminal publication below legal fanout");
_Static_assert(SEQ_ENGINE_FX_SCRATCH_CAPACITY == 32U, "SEQ scratch contract");

#endif
