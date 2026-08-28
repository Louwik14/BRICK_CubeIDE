#ifndef SEQ_RUNTIME_EXEC_H
#define SEQ_RUNTIME_EXEC_H

#include "Seq/seq_runtime.h"
#include "Seq/seq_clock_bridge.h"
#include "Seq/seq_transport_fsm.h"

#define SEQ_RUNTIME_EXEC_MAX_EXTERNAL_PULSES_PER_BLOCK 4U

seq_runtime_state_t *seq_runtime_exec_state(void);
/*
 * Contract surface:
 * - readonly mirror of the shared execution owner.
 * - exposes timeline/progression state for consumers that need projection only.
 * - does not grant mutation ownership.
 */
const seq_runtime_state_t *seq_runtime_exec_state_const(void);

/*
 * Contract surface:
 * - execution state ownership: timeline, step progression, transport execution, audio block state.
 * - all functions here are the explicit runtime-execution seam used by seq_runtime.
 */
void seq_runtime_exec_init(void);
void seq_runtime_exec_reset_sample_timeline(uint64_t start_sample);
/*
 * Contract surface:
 * - timeline projection only.
 * - returns the current audio-block timeline sample owned by runtime-exec.
 */
uint64_t seq_runtime_exec_get_sample_timeline(void);
void seq_runtime_exec_prepare_start_lifecycle(seq_runtime_state_t *state,
                                              seq_clock_bridge_t *clock_bridge,
                                              uint32_t now_tick);
/*
 * Contract surface:
 * - execution lifecycle helper for the first RUNNING sample point.
 * - initializes runtime execution state, then allows scheduler seeding.
 * - transport ownership remains outside this helper.
 */
void seq_runtime_exec_begin_running_at_sample_q16(seq_runtime_state_t *state,
                                                  seq_transport_fsm_t *transport_fsm,
                                                  seq_clock_bridge_t *clock_bridge,
                                                  uint32_t now_tick,
                                                  uint64_t start_sample_q16);
/*
 * Contract surface:
 * - execution lifecycle helper for STOP / flush.
 * - clears runtime execution state and scheduler queue, but does not own transport policy.
 */
void seq_runtime_exec_stop_lifecycle_apply(seq_runtime_state_t *state);
void seq_runtime_exec_set_midi_clock_enabled(uint8_t enabled);
void seq_runtime_exec_set_midi_clock_period_q16(uint32_t period_q16);
uint32_t seq_runtime_exec_get_midi_clock_period_q16(void);
void seq_runtime_exec_rebase_midi_clock(uint64_t start_sample);
void seq_runtime_exec_emit_midi_clock_for_block(uint64_t block_start_sample,
                                                uint16_t block_frames,
                                                seq_clock_src_t clock_src,
                                                uint8_t running);
void seq_runtime_exec_process_step_pulse_at_sample_q16(seq_runtime_state_t *state,
                                                       seq_transport_fsm_t *transport_fsm,
                                                       seq_clock_bridge_t *clock_bridge,
                                                       uint32_t *track_loop_generation,
                                                       uint64_t pulse_sample_q16,
                                                       uint32_t now_tick,
                                                       uint64_t now_sample);
/*
 * Contract surface:
 * - internal block helpers: they turn cadence into step advancement in the runtime domain.
 * - they do not own the scheduler queue, only trigger scheduling from resolved boundaries.
 */
void seq_runtime_exec_drive_internal_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames);
/*
 * Contract surface:
 * - internal block helper for external cadence pending pulses.
 * - consumes pending pulses in the audio block domain, then routes through pulse processing.
 */
void seq_runtime_exec_drive_external_steps_for_block(seq_runtime_state_t *state,
                                                     seq_transport_fsm_t *transport_fsm,
                                                     seq_clock_bridge_t *clock_bridge,
                                                     uint32_t *track_loop_generation,
                                                     seq_clock_src_t clock_src,
                                                     uint32_t now_tick,
                                                     uint64_t block_start_sample,
                                                     uint16_t block_frames);
void seq_runtime_exec_set_external_step_pulses_pending(uint32_t pending);
void seq_runtime_exec_increment_external_step_pulses_pending(void);
uint32_t seq_runtime_exec_consume_external_step_pulses_pending(void);
uint32_t seq_runtime_exec_external_step_pulses_pending(void);
/*
 * Contract surface:
 * - block-domain convergence point between progression/timeline and event scheduling.
 * - advances execution timeline, drives step pulses, then drains scheduler events.
 * - scheduler owns the event queue; this seam only orchestrates block execution.
 */
uint16_t seq_runtime_exec_collect_block_events(seq_runtime_state_t *state,
                                               seq_transport_fsm_t *transport_fsm,
                                               seq_clock_bridge_t *clock_bridge,
                                               uint32_t *track_loop_generation,
                                               seq_runtime_control_event_t *out_events,
                                               uint16_t max_events,
                                               uint64_t block_start_sample,
                                               uint16_t block_frames,
                                               seq_clock_src_t clock_src,
                                               uint8_t running);
uint16_t seq_runtime_exec_collect_remaining_scheduler_events(
    seq_runtime_control_event_t *out_events,
    uint16_t max_events,
    uint16_t block_frames,
    uint64_t block_start_sample);

#endif /* SEQ_RUNTIME_EXEC_H */
