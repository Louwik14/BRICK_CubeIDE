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
    seq_clock_src_t clock_src;
    uint8_t play_step[SEQ_TRACK_COUNT];
    uint8_t prev_step[SEQ_TRACK_COUNT];
    uint8_t prev_step_valid[SEQ_TRACK_COUNT];
    uint8_t active_lock_count[SEQ_TRACK_COUNT];
    uint8_t track_div[SEQ_TRACK_COUNT];
    uint8_t track_quant[SEQ_TRACK_COUNT];
    uint8_t track_swing[SEQ_TRACK_COUNT];
    uint32_t last_tick_count;
    uint32_t tick_accum;
    uint16_t ticks_per_step;
    uint8_t ext_clock_tick_accum;
    uint8_t reserved;
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

void seq_runtime_init(void);
void seq_runtime_process(void);
const seq_runtime_state_t *seq_runtime_get_state(void);

void seq_runtime_start(void);
void seq_runtime_stop(void);
void seq_runtime_toggle_play_stop(void);
uint8_t seq_runtime_is_running(void);

void seq_runtime_set_clock_source(seq_clock_src_t src);
seq_clock_src_t seq_runtime_get_clock_source(void);
void seq_runtime_midi_clock(void);
void seq_runtime_midi_clock_from_source(seq_clock_src_t source);
void seq_runtime_midi_start(void);
void seq_runtime_midi_start_from_source(seq_clock_src_t source);
void seq_runtime_midi_continue(void);
void seq_runtime_midi_continue_from_source(seq_clock_src_t source);
void seq_runtime_midi_stop(void);
void seq_runtime_midi_stop_from_source(seq_clock_src_t source);

uint8_t seq_runtime_set_playhead_step(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_runtime_get_playhead_step(seq_track_id_t track, seq_step_id_t *out_step);

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div);
void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant);
void seq_runtime_set_track_swing(seq_track_id_t track, uint8_t swing);

void seq_runtime_rec_toggle_arm(void);
uint8_t seq_runtime_rec_is_armed(void);
void seq_runtime_set_rec_count_in_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_count_in_mode(void);
void seq_runtime_set_rec_len_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_len_mode(void);
uint32_t seq_runtime_get_rec_count_in_remaining_steps(void);
uint8_t seq_runtime_rec_is_pattern_pending_start(void);
uint32_t seq_runtime_get_tempo_bpm_milli(void);
void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli);
uint8_t seq_runtime_is_external_tempo_valid(void);
uint32_t seq_runtime_get_external_tempo_bpm_milli(void);
void seq_runtime_live_rec_note_on(seq_live_rec_source_t source,
                                  uint8_t channel_zero_based,
                                  uint8_t note,
                                  uint8_t velocity);
void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note);

#endif /* SEQ_RUNTIME_H */
