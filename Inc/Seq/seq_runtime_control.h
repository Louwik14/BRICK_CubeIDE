#ifndef SEQ_RUNTIME_CONTROL_H
#define SEQ_RUNTIME_CONTROL_H

#include <stdint.h>

#include "Seq/seq_runtime.h"
#include "Seq/seq_model.h"

void seq_runtime_set_clock_source(seq_clock_src_t src);
seq_clock_src_t seq_runtime_get_clock_source(void);

uint8_t seq_runtime_set_playhead_step(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_runtime_get_playhead_step(seq_track_id_t track, seq_step_id_t *out_step);
void seq_runtime_on_track_length_changed(seq_track_id_t track);
void seq_runtime_on_step_play_changed(seq_track_id_t track,
                                      seq_step_id_t step,
                                      uint8_t voice,
                                      seq_step_play_field_t field);
void seq_runtime_on_step_play_removed(seq_track_id_t track,
                                      seq_step_id_t step,
                                      int16_t voice);
void seq_runtime_on_step_roll_changed(seq_track_id_t track,
                                      seq_step_id_t step);
void seq_runtime_clear_tracks(const seq_track_id_t *tracks, uint8_t track_count);
void seq_runtime_set_tracks_muted(const seq_track_id_t *tracks, uint8_t track_count, uint8_t muted);
void seq_runtime_begin_track_restore(const seq_track_id_t *tracks, uint8_t track_count);
void seq_runtime_end_track_restore(const seq_track_id_t *tracks, uint8_t track_count);
void seq_runtime_control_request_from_audio_irq(void);
void seq_runtime_control_service_from_pendsv(void);

void seq_runtime_set_track_div(seq_track_id_t track, uint8_t div);
void seq_runtime_restore_track_div(seq_track_id_t track, uint8_t div);
uint8_t seq_runtime_get_track_div(seq_track_id_t track, uint8_t *out_div);
void seq_runtime_set_track_quant(seq_track_id_t track, uint8_t quant);
uint8_t seq_runtime_get_track_quant(seq_track_id_t track, uint8_t *out_quant);
void seq_runtime_set_track_swing(seq_track_id_t track, uint8_t swing);
uint8_t seq_runtime_get_track_swing(seq_track_id_t track, uint8_t *out_swing);

void seq_runtime_rec_toggle_arm(void);
void seq_runtime_set_pattern_rec_target_track(seq_track_id_t track);
uint8_t seq_runtime_rec_is_armed(void);
void seq_runtime_set_rec_start_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_start_mode(void);
uint8_t seq_runtime_rec_is_waiting_trigger_start(void);
void seq_runtime_set_rec_len_mode(uint8_t mode);
uint8_t seq_runtime_get_rec_len_mode(void);
uint32_t seq_runtime_get_rec_count_in_remaining_steps(void);
uint8_t seq_runtime_rec_is_pattern_pending_start(void);
uint32_t seq_runtime_get_tempo_bpm_milli(void);
void seq_runtime_set_tempo_bpm_milli(uint32_t bpm_milli);
uint8_t seq_runtime_is_external_tempo_valid(void);
uint32_t seq_runtime_get_external_tempo_bpm_milli(void);

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
void seq_runtime_live_rec_note_off(seq_live_rec_source_t source,
                                   uint8_t channel_zero_based,
                                   uint8_t note);

#endif /* SEQ_RUNTIME_CONTROL_H */
