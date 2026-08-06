#ifndef SEQ_LIVE_REC_SESSION_H
#define SEQ_LIVE_REC_SESSION_H

#include <stdint.h>

#include "Seq/seq_runtime.h"

void seq_live_rec_session_init(void);
void seq_live_rec_session_reset_capture(void);
void seq_live_rec_session_on_transport_start(void);
void seq_live_rec_session_on_transport_stop(uint64_t stop_sample, uint32_t samples_per_step_q16);
void seq_live_rec_session_on_step_advanced(const seq_runtime_state_t *runtime_state, uint64_t now_sample);

void seq_live_rec_session_toggle_arm(uint64_t now_sample, uint32_t samples_per_step_q16);
uint8_t seq_live_rec_session_rec_is_armed(void);
void seq_live_rec_session_set_rec_start_mode(uint8_t mode);
uint8_t seq_live_rec_session_get_rec_start_mode(void);
uint8_t seq_live_rec_session_rec_should_wait_trigger_start(void);
uint8_t seq_live_rec_session_consume_trigger_start_note_on(void);
void seq_live_rec_session_clear_trigger_start_wait(void);
uint8_t seq_live_rec_session_rec_is_waiting_trigger_start(void);
void seq_live_rec_session_set_rec_len_mode(uint8_t mode);
uint8_t seq_live_rec_session_get_rec_len_mode(void);
uint8_t seq_live_rec_session_rec_is_pattern_pending_start(void);
void seq_live_rec_session_set_pattern_rec_target_track(seq_track_id_t track);

uint8_t seq_live_rec_session_live_rec_param_can_write(seq_track_id_t track,
                                                      uint8_t set_id,
                                                      seq_param_slot_t param_slot);
uint8_t seq_live_rec_session_live_rec_param_write(const seq_runtime_state_t *runtime_state,
                                                  seq_track_id_t track,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot,
                                                  seq_value16_t value16);
void seq_live_rec_session_live_rec_note_on(seq_live_rec_source_t source,
                                           uint8_t channel_zero_based,
                                           uint8_t note,
                                           uint8_t velocity,
                                           const seq_runtime_state_t *runtime_state,
                                           uint64_t now_sample,
                                           uint32_t occurrence_id);
void seq_live_rec_session_live_rec_note_off(seq_live_rec_source_t source,
                                            uint8_t channel_zero_based,
                                            uint8_t note,
                                            const seq_runtime_state_t *runtime_state,
                                            uint64_t now_sample,
                                            uint32_t occurrence_id);

#endif /* SEQ_LIVE_REC_SESSION_H */
