#ifndef SEQ_RUNTIME_H
#define SEQ_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint8_t active;
    uint8_t set_id;
    seq_param8_t param8;
    uint8_t reserved;
    seq_value16_t base_value16;
} seq_runtime_active_lock_t;

typedef struct
{
    uint8_t running;
    uint8_t play_step[SEQ_TRACK_COUNT];
    uint8_t prev_step[SEQ_TRACK_COUNT];
    uint8_t prev_step_valid[SEQ_TRACK_COUNT];
    uint8_t active_lock_count[SEQ_TRACK_COUNT];
    uint8_t track_div_phase[SEQ_TRACK_COUNT];
    uint32_t last_tick_count;
    uint32_t tick_accum;
    uint16_t ticks_per_step;
    uint8_t ext_clock_tick_accum;
    uint8_t reserved;
    uint64_t step_sample_q16;
    uint32_t samples_per_step_q16;
    uint64_t audio_block_start_sample;
    uint64_t audio_timeline_sample;
    seq_runtime_active_lock_t active_locks[SEQ_TRACK_COUNT][SEQ_STEP_MAX_LOCKS];
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

typedef struct
{
    uint8_t type;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint16_t sample_offset_in_block;
} seq_runtime_audio_event_t;

typedef struct
{
    uint32_t internal_irq_tick_count;
    uint32_t internal_non_audio_step_pulse_count;
    uint32_t internal_step_burst_block_count;
    uint16_t max_internal_step_pulses_per_block;
} seq_runtime_diag_t;

void seq_runtime_init(void);
void seq_runtime_time_adapter_process_internal_from_irq(void);
void seq_runtime_time_adapter_process(void);
uint16_t seq_runtime_audio_collect_block_events(seq_runtime_audio_event_t *out_events,
                                                uint16_t max_events,
                                                uint16_t block_frames);
void seq_runtime_audio_apply_event(const seq_runtime_audio_event_t *event);
const seq_runtime_state_t *seq_runtime_get_state(void);

void seq_runtime_start(void);
void seq_runtime_stop(void);
void seq_runtime_toggle_play_stop(void);
uint8_t seq_runtime_is_running(void);
uint8_t seq_runtime_is_start_pending(void);
uint8_t seq_runtime_rec_is_armed(void);
void seq_runtime_set_rec_count_in_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_count_in_mode(void);
void seq_runtime_set_rec_len_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_len_mode(void);
uint32_t seq_runtime_get_rec_count_in_remaining_steps(void);
uint8_t seq_runtime_rec_is_pattern_pending_start(void);
void seq_runtime_set_pattern_rec_target_track(seq_track_id_t track);
void seq_runtime_rec_toggle_arm(void);
uint8_t seq_runtime_live_rec_param_write(seq_track_id_t track,
                                         uint8_t set_id,
                                         seq_param8_t param8,
                                         seq_value16_t value16);
uint8_t seq_runtime_live_rec_param_can_write(seq_track_id_t track,
                                             uint8_t set_id,
                                             seq_param8_t param8);
void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity);
void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note);
uint8_t seq_runtime_get_track_loop_generation(seq_track_id_t track, uint32_t *out_generation);
void seq_runtime_midi_clock_from_source(seq_clock_src_t source);
void seq_runtime_midi_start_from_source(seq_clock_src_t source);
void seq_runtime_midi_continue_from_source(seq_clock_src_t source);
void seq_runtime_midi_stop_from_source(seq_clock_src_t source);
void seq_runtime_on_midi_program_live_change(uint8_t track, float program_value);
void seq_runtime_on_track_pattern_change(uint8_t track);
void seq_runtime_diag_reset(void);
void seq_runtime_diag_snapshot(seq_runtime_diag_t *out_diag);

#endif /* SEQ_RUNTIME_H */
