#ifndef SEQ_CLOCK_BRIDGE_H
#define SEQ_CLOCK_BRIDGE_H

#include <stdint.h>

#include "Seq/seq_types.h"
#include "Seq/seq_runtime.h"

typedef struct
{
    uint32_t tempo_bpm_milli;
    uint32_t ext_clock_last_tick;
    uint32_t ext_clock_period_accum;
    uint16_t ext_clock_period_samples;
    uint8_t ext_clock_tempo_valid;
    uint32_t ext_clock_bpm_milli;
    uint32_t internal_step_ticks_base;
    uint32_t internal_step_ticks_rem;
    uint32_t internal_step_ticks_den;
    uint32_t internal_step_ticks_rem_accum;
    uint32_t internal_next_step_ticks;
} seq_clock_bridge_t;

void seq_clock_bridge_init(seq_clock_bridge_t *bridge,
                           seq_runtime_state_t *runtime,
                           uint32_t default_tempo_bpm_milli);
uint8_t seq_clock_bridge_is_external_source(seq_clock_src_t src);
/*
 * Contract surface:
 * - clock-source and tempo policy live here.
 * - converts tempo/ticks and external clock pulses into cadence inputs.
 * - transport ownership remains in seq_transport_fsm.
 */
void seq_clock_bridge_reset_external_tempo(seq_clock_bridge_t *bridge);
void seq_clock_bridge_on_process(seq_clock_bridge_t *bridge,
                                 seq_clock_src_t active_src,
                                 uint32_t engine_ticks_now);
void seq_clock_bridge_set_source(seq_clock_bridge_t *bridge,
                                 seq_runtime_state_t *runtime,
                                 seq_clock_src_t src);
void seq_clock_bridge_prepare_internal_run(seq_clock_bridge_t *bridge);
/*
 * Contract surface:
 * - internal cadence accumulator only.
 * - consumes the next due internal step from accumulated tick budget.
 * - transport state remains owned by seq_transport_fsm.
 */
uint8_t seq_clock_bridge_consume_internal_step_due(seq_clock_bridge_t *bridge,
                                                   uint32_t *tick_accum);
uint8_t seq_clock_bridge_on_external_clock_pulse(seq_clock_bridge_t *bridge,
                                                 seq_runtime_state_t *runtime,
                                                 seq_clock_src_t active_src,
                                                 seq_clock_src_t source,
                                                 uint32_t engine_ticks_now,
                                                 uint8_t *out_step_pulse);

void seq_clock_bridge_set_internal_tempo(seq_clock_bridge_t *bridge,
                                         seq_runtime_state_t *runtime,
                                         uint32_t bpm_milli);
uint32_t seq_clock_bridge_get_internal_tempo_bpm_milli(const seq_clock_bridge_t *bridge);
uint8_t seq_clock_bridge_is_external_tempo_valid(const seq_clock_bridge_t *bridge);
uint32_t seq_clock_bridge_get_external_tempo_bpm_milli(const seq_clock_bridge_t *bridge);

#endif /* SEQ_CLOCK_BRIDGE_H */
