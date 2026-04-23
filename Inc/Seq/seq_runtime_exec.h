#ifndef SEQ_RUNTIME_EXEC_H
#define SEQ_RUNTIME_EXEC_H

#include "Seq/seq_runtime.h"
#include "Seq/seq_clock_bridge.h"
#include "Seq/seq_transport_fsm.h"

seq_runtime_state_t *seq_runtime_exec_state(void);
const seq_runtime_state_t *seq_runtime_exec_state_const(void);
void seq_runtime_exec_init(void);
void seq_runtime_exec_reset_audio_timeline(uint64_t start_sample);
uint64_t seq_runtime_exec_get_audio_timeline_sample(void);
uint64_t seq_runtime_exec_begin_audio_block(uint16_t block_frames);
void seq_runtime_exec_prepare_start_lifecycle(seq_runtime_state_t *state,
                                              seq_clock_bridge_t *clock_bridge,
                                              uint32_t now_tick);
void seq_runtime_exec_begin_running_at_sample_q16(seq_runtime_state_t *state,
                                                  seq_transport_fsm_t *transport_fsm,
                                                  seq_clock_bridge_t *clock_bridge,
                                                  uint32_t now_tick,
                                                  uint64_t start_sample_q16);
void seq_runtime_exec_stop_lifecycle_apply(seq_runtime_state_t *state);
void seq_runtime_exec_set_midi_clock_audio_enabled(uint8_t enabled);
void seq_runtime_exec_set_midi_clock_period_q16(uint32_t period_q16);
uint32_t seq_runtime_exec_get_midi_clock_period_q16(void);
void seq_runtime_exec_rebase_midi_clock_audio(uint64_t start_sample);
void seq_runtime_exec_emit_midi_clock_for_block(uint64_t block_start_sample,
                                                uint16_t block_frames,
                                                seq_clock_src_t clock_src,
                                                uint8_t running);
void seq_runtime_exec_process_step_pulse_at_sample_q16(seq_runtime_state_t *state,
                                                       seq_transport_fsm_t *transport_fsm,
                                                       seq_clock_bridge_t *clock_bridge,
                                                       seq_runtime_diag_t *diag,
                                                       uint32_t *track_loop_generation,
                                                       uint64_t pulse_sample_q16,
                                                       uint32_t now_tick,
                                                       uint64_t now_sample);
void seq_runtime_exec_drive_internal_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     seq_runtime_diag_t *diag,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames);
void seq_runtime_exec_drive_external_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames);
void seq_runtime_exec_set_external_step_pulses_pending(uint16_t pending);
void seq_runtime_exec_increment_external_step_pulses_pending(void);
uint16_t seq_runtime_exec_consume_external_step_pulses_pending(void);
uint16_t seq_runtime_exec_collect_block_events(seq_runtime_state_t *state,
                                               seq_transport_fsm_t *transport_fsm,
                                               seq_clock_bridge_t *clock_bridge,
                                               seq_runtime_diag_t *diag,
                                               uint32_t *track_loop_generation,
                                               seq_runtime_audio_event_t *out_events,
                                               uint16_t max_events,
                                               uint16_t block_frames,
                                               seq_clock_src_t clock_src,
                                               uint8_t running);

#endif /* SEQ_RUNTIME_EXEC_H */
