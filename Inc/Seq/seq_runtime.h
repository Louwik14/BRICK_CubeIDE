#ifndef SEQ_RUNTIME_H
#define SEQ_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_types.h"

#define SEQ_RUNTIME_AUDIO_EVENT_BOUNDARY_EDGE 0xF0U
#define SEQ_RUNTIME_AUDIO_EVENT_METRO_CLICK   0xF1U

typedef struct
{
    uint8_t active;
    uint8_t set_id;
    seq_param_slot_t param_slot;
    uint8_t reserved;
    seq_value16_t base_value16;
} seq_runtime_active_lock_t;

typedef struct
{
    uint8_t running;
    uint8_t play_step[SEQ_LANE_CAPACITY];
    uint8_t prev_step[SEQ_LANE_CAPACITY];
    uint8_t prev_step_valid[SEQ_LANE_CAPACITY];
    uint8_t active_lock_count[SEQ_LANE_CAPACITY];
    uint8_t track_div_phase[SEQ_LANE_CAPACITY];
    uint8_t track_swing_phase[SEQ_LANE_CAPACITY];
    uint32_t last_tick_count;
    uint32_t tick_accum;
    uint16_t ticks_per_step;
    uint8_t ext_clock_tick_accum;
    uint8_t reserved;
    uint64_t step_sample_q16;
    uint32_t samples_per_step_q16;
    uint64_t audio_block_start_sample;
    uint64_t audio_timeline_sample;
    seq_runtime_active_lock_t active_locks[SEQ_LANE_CAPACITY][SEQ_STEP_MAX_LOCKS];
} seq_runtime_state_t;

typedef enum
{
    SEQ_LIVE_REC_SRC_INTERNAL = 0,
    SEQ_LIVE_REC_SRC_EXTERNAL
} seq_live_rec_source_t;

typedef enum
{
    SEQ_REC_LEN_MODE_OVERDUB = 0,
    SEQ_REC_LEN_MODE_PATTERN = 1
} seq_rec_len_mode_t;

typedef enum
{
    SEQ_REC_START_DEFAULT = 0,
    SEQ_REC_START_TRIG = 1,
    SEQ_REC_START_ROLL_1_4 = 2,
    SEQ_REC_START_ROLL_1_2 = 3,
    SEQ_REC_START_ROLL_1 = 4
} seq_rec_start_mode_t;

typedef struct
{
    uint8_t type;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t track_generation;
    uint8_t reserved;
    uint16_t sample_offset_in_block;
    uint64_t sample_abs;
    uint32_t generation;
    uint32_t event_token;
} seq_runtime_control_event_t;

/*
 * Contract surface:
 * - orchestration / policy / event routing: lifecycle, transport, live-rec, clock policy.
 * - queries: pure reads of runtime/control state and diagnostics.
 * - the shared execution state is owned by seq_runtime_exec; seq_runtime uses it as facade.
 */
void seq_runtime_init(void);
/* Notification/maintenance seam: IRQ tick accounting only, no step authority. */
void seq_runtime_time_adapter_process_internal_from_irq(void);
/* Orchestration loop: supervises transport, clock source and external/internal progress. */
void seq_runtime_time_adapter_process(void);
uint32_t seq_runtime_get_samples_per_step_q16(void);

/* Orchestration / policy surface. */
void seq_runtime_start(void);
void seq_runtime_stop(void);
void seq_runtime_toggle_play_stop(void);
void seq_runtime_set_rec_start_mode(uint8_t mode);
void seq_runtime_set_rec_len_mode(uint8_t mode);
void seq_runtime_set_pattern_rec_target_track(seq_track_id_t track);
void seq_runtime_rec_toggle_arm(void);
/* Command surface: live-rec write routed through the runtime policy layer. */
/* Dedicated PLAY target marker; never a generic p-lock set id. */
enum { SEQ_LIVE_REC_PARAM_SET_PLAY = UINT8_MAX };

uint8_t seq_runtime_live_rec_param_write(seq_track_id_t track,
                                         uint8_t set_id,
                                         seq_param_slot_t param_slot,
                                         seq_value16_t value16);
uint8_t seq_runtime_live_rec_param_can_write(seq_track_id_t track,
                                             uint8_t set_id,
                                             seq_param_slot_t param_slot);
uint8_t seq_runtime_live_rec_param_resolve_write_step(seq_track_id_t track,
                                                      uint8_t set_id,
                                                      seq_param_slot_t param_slot,
                                                      seq_step_id_t *out_step);
void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity);
void seq_runtime_live_rec_note_on_at_sample(seq_live_rec_source_t source,
                                            uint8_t channel_zero_based,
                                            uint8_t note,
                                            uint8_t velocity,
                                            uint64_t sample_time);
void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note);
void seq_runtime_live_rec_note_off_at_sample(seq_live_rec_source_t source,
                                             uint8_t channel_zero_based,
                                             uint8_t note,
                                             uint64_t sample_time);
/* Audio-owned effective-time handoff; drained outside the audio IRQ. */
uint8_t seq_runtime_live_rec_submit_effective(seq_live_rec_source_t source,
                                              uint8_t is_note_on,
                                              uint8_t channel_zero_based,
                                              uint8_t note,
                                              uint8_t velocity,
                                              uint64_t effective_sample_time,
                                              uint32_t ingress_serial,
                                              uint32_t occurrence_id);
void seq_runtime_live_rec_drain_effective(void);
/* Notification surface from MIDI input / transport source. */
void seq_runtime_midi_clock_from_source(seq_clock_src_t source);
void seq_runtime_midi_start_from_source(seq_clock_src_t source);
void seq_runtime_midi_continue_from_source(seq_clock_src_t source);
void seq_runtime_midi_stop_from_source(seq_clock_src_t source);
void seq_runtime_on_midi_program_live_change(uint8_t track, float program_value);
void seq_runtime_on_track_pattern_change(uint8_t track);

/* Queries. */
uint8_t seq_runtime_is_running(void);
uint8_t seq_runtime_is_start_pending(void);
uint8_t seq_runtime_rec_is_armed(void);
uint8_t seq_runtime_get_rec_start_mode(void);
uint8_t seq_runtime_rec_is_waiting_trigger_start(void);
uint8_t seq_runtime_get_rec_len_mode(void);
uint32_t seq_runtime_get_rec_count_in_remaining_steps(void);
uint32_t seq_runtime_get_tempo_bpm_milli(void);
uint8_t seq_runtime_rec_is_pattern_pending_start(void);
uint8_t seq_runtime_get_track_loop_generation(seq_track_id_t track, uint32_t *out_generation);
uint8_t seq_runtime_get_track_next_loop_sample(seq_track_id_t track,
                                               uint64_t *out_sample);
#endif /* SEQ_RUNTIME_H */
