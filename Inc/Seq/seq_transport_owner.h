#ifndef SEQ_TRANSPORT_OWNER_H
#define SEQ_TRANSPORT_OWNER_H
#include <stdint.h>
#include "Seq/seq_runtime.h"
#include "Seq/seq_clock_bridge.h"
#include "Seq/seq_transport_fsm.h"

seq_runtime_state_t *seq_transport_owner_state(void);
void seq_transport_owner_init(void);
void seq_transport_owner_reset_sample_timeline(uint64_t sample);
uint64_t seq_transport_owner_get_sample_timeline(void);
uint32_t seq_transport_owner_get_transport_step(void);
void seq_transport_owner_prepare_start_lifecycle(seq_runtime_state_t *state,
    seq_clock_bridge_t *clock_bridge,uint32_t now_tick);
void seq_transport_owner_begin_running_at_sample_q16(seq_runtime_state_t *state,
    seq_transport_fsm_t *fsm,seq_clock_bridge_t *clock_bridge,
    uint32_t now_tick,uint64_t start_q16);
void seq_transport_owner_stop_lifecycle_apply(seq_runtime_state_t *state,
    uint64_t effective_sample);
void seq_transport_owner_set_midi_clock_enabled(uint8_t enabled);
void seq_transport_owner_set_midi_clock_period_q16(uint32_t period_q16);
void seq_transport_owner_rebase_midi_clock(uint64_t sample);
void seq_transport_owner_set_external_step_pulses_pending(uint32_t pending);
void seq_transport_owner_increment_external_step_pulses_pending(void);
uint32_t seq_transport_owner_external_step_pulses_pending(void);
void seq_transport_owner_enqueue_transport_start(uint64_t sample);
#endif
