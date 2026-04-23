/*
 * Module: seq_clock_bridge
 * Role: Pont d'horloge entre tempo interne moteur et sources externes MIDI/USB.
 * Responsibilities: clamp tempo, convertir BPM<->périodes de tick/step,
 * estimer le tempo externe et produire les impulsions de step consommées par runtime.
 * Integration: utilisé par seq_runtime; ne déclenche pas directement les notes.
 */
#include "Seq/seq_clock_bridge.h"

#define SEQ_CLOCK_BRIDGE_MIDI_CLOCKS_PER_STEP 6U
#define SEQ_CLOCK_BRIDGE_STEPS_PER_QUARTER_NOTE 4U
#define SEQ_CLOCK_BRIDGE_ENGINE_TICK_HZ 1500U
#define SEQ_CLOCK_BRIDGE_EXT_TEMPO_TIMEOUT_TICKS (SEQ_CLOCK_BRIDGE_ENGINE_TICK_HZ * 2U)

static uint32_t seq_clock_bridge_clamp_tempo(uint32_t bpm_milli)
{
    if (bpm_milli < 40000U)
    {
        return 40000U;
    }

    if (bpm_milli > 300000U)
    {
        return 300000U;
    }

    return bpm_milli;
}

static void seq_clock_bridge_internal_step_period_recompute(seq_clock_bridge_t *bridge,
                                                            seq_runtime_state_t *runtime)
{
    const uint32_t bpm_milli = seq_clock_bridge_clamp_tempo(bridge->tempo_bpm_milli);
    const uint64_t num = ((uint64_t)SEQ_CLOCK_BRIDGE_ENGINE_TICK_HZ * 60ULL * 1000ULL);
    const uint32_t den = (uint32_t)((uint64_t)bpm_milli * (uint64_t)SEQ_CLOCK_BRIDGE_STEPS_PER_QUARTER_NOTE);
    uint32_t base = (uint32_t)(num / den);
    if (base == 0U)
    {
        base = 1U;
    }

    bridge->internal_step_ticks_base = base;
    bridge->internal_step_ticks_rem = (uint32_t)(num % den);
    bridge->internal_step_ticks_den = den;
    bridge->internal_step_ticks_rem_accum = 0U;
    bridge->internal_next_step_ticks = base;

    const uint32_t rounded_tps = (uint32_t)((num + ((uint64_t)den / 2ULL)) / (uint64_t)den);
    runtime->ticks_per_step = (uint16_t)((rounded_tps == 0U) ? 1U : rounded_tps);
}

void seq_clock_bridge_init(seq_clock_bridge_t *bridge,
                           seq_runtime_state_t *runtime,
                           uint32_t default_tempo_bpm_milli)
{
    if ((bridge == 0) || (runtime == 0))
    {
        return;
    }

    bridge->tempo_bpm_milli = seq_clock_bridge_clamp_tempo(default_tempo_bpm_milli);
    seq_clock_bridge_reset_external_tempo(bridge);
    seq_clock_bridge_internal_step_period_recompute(bridge, runtime);
}

uint8_t seq_clock_bridge_is_external_source(seq_clock_src_t src)
{
    return ((src == SEQ_CLOCK_SRC_EXTERNAL_MIDI) || (src == SEQ_CLOCK_SRC_EXTERNAL_USB)) ? 1U : 0U;
}

void seq_clock_bridge_reset_external_tempo(seq_clock_bridge_t *bridge)
{
    if (bridge == 0)
    {
        return;
    }

    bridge->ext_clock_last_tick = 0U;
    bridge->ext_clock_period_accum = 0U;
    bridge->ext_clock_period_samples = 0U;
    bridge->ext_clock_tempo_valid = 0U;
    bridge->ext_clock_bpm_milli = 0U;
}

void seq_clock_bridge_on_process(seq_clock_bridge_t *bridge,
                                 seq_clock_src_t active_src,
                                 uint32_t engine_ticks_now)
{
    /* Hybrid seam: this supervises clock policy only; transport state remains in seq_transport_fsm. */
    if ((bridge == 0) || (seq_clock_bridge_is_external_source(active_src) == 0U) || (bridge->ext_clock_last_tick == 0U))
    {
        return;
    }

    const uint32_t silent_ticks = engine_ticks_now - bridge->ext_clock_last_tick;
    if (silent_ticks > SEQ_CLOCK_BRIDGE_EXT_TEMPO_TIMEOUT_TICKS)
    {
        bridge->ext_clock_tempo_valid = 0U;
        bridge->ext_clock_bpm_milli = 0U;
        bridge->ext_clock_period_accum = 0U;
        bridge->ext_clock_period_samples = 0U;
    }
}

void seq_clock_bridge_set_source(seq_clock_bridge_t *bridge,
                                 seq_runtime_state_t *runtime,
                                 seq_clock_src_t src)
{
    /* Hybrid seam: source change resets cadence policy and runtime tick accumulators, but not transport state. */
    if ((bridge == 0) || (runtime == 0))
    {
        return;
    }

    runtime->ext_clock_tick_accum = 0U;
    runtime->tick_accum = 0U;
    seq_clock_bridge_reset_external_tempo(bridge);

    if (seq_clock_bridge_is_external_source(src) == 0U)
    {
        seq_clock_bridge_internal_step_period_recompute(bridge, runtime);
        seq_clock_bridge_prepare_internal_run(bridge);
    }
}

void seq_clock_bridge_prepare_internal_run(seq_clock_bridge_t *bridge)
{
    if (bridge == 0)
    {
        return;
    }

    bridge->internal_next_step_ticks = bridge->internal_step_ticks_base;
}

uint8_t seq_clock_bridge_consume_internal_step_due(seq_clock_bridge_t *bridge,
                                                   uint32_t *tick_accum)
{
    /* Hybrid seam: internal cadence counter only; transport state remains owned elsewhere. */
    if ((bridge == 0) || (tick_accum == 0))
    {
        return 0U;
    }

    if (*tick_accum < bridge->internal_next_step_ticks)
    {
        return 0U;
    }

    *tick_accum -= bridge->internal_next_step_ticks;

    uint32_t ticks = bridge->internal_step_ticks_base;
    if (bridge->internal_step_ticks_den != 0U)
    {
        uint32_t rem_accum = bridge->internal_step_ticks_rem_accum + bridge->internal_step_ticks_rem;
        if (rem_accum >= bridge->internal_step_ticks_den)
        {
            rem_accum -= bridge->internal_step_ticks_den;
            ticks += 1U;
        }
        bridge->internal_step_ticks_rem_accum = rem_accum;
    }

    bridge->internal_next_step_ticks = (ticks == 0U) ? 1U : ticks;
    return 1U;
}

uint8_t seq_clock_bridge_on_external_clock_pulse(seq_clock_bridge_t *bridge,
                                                 seq_runtime_state_t *runtime,
                                                 seq_clock_src_t active_src,
                                                 seq_clock_src_t source,
                                                 uint32_t engine_ticks_now,
                                                 uint8_t *out_step_pulse)
{
    /* Hybrid seam: external clock pulses update cadence policy and emit a step request; transport owns the actual step progression. */
    if ((bridge == 0) || (runtime == 0) || (out_step_pulse == 0))
    {
        return 0U;
    }

    *out_step_pulse = 0U;
    if (active_src != source)
    {
        return 0U;
    }

    if (bridge->ext_clock_last_tick != 0U)
    {
        const uint32_t delta = engine_ticks_now - bridge->ext_clock_last_tick;
        if ((delta > 0U) && (delta < SEQ_CLOCK_BRIDGE_EXT_TEMPO_TIMEOUT_TICKS))
        {
            bridge->ext_clock_period_accum += delta;
            if (bridge->ext_clock_period_samples < 0xFFFFU)
            {
                bridge->ext_clock_period_samples++;
            }
            if (bridge->ext_clock_period_samples >= 24U)
            {
                const uint32_t avg_delta = bridge->ext_clock_period_accum / (uint32_t)bridge->ext_clock_period_samples;
                if (avg_delta > 0U)
                {
                    bridge->ext_clock_bpm_milli =
                            (uint32_t)(((uint64_t)SEQ_CLOCK_BRIDGE_ENGINE_TICK_HZ * 60ULL * 1000ULL)
                                       / ((uint64_t)avg_delta * 24ULL));
                    bridge->ext_clock_tempo_valid = 1U;
                }
            }
        }
        else
        {
            bridge->ext_clock_period_accum = 0U;
            bridge->ext_clock_period_samples = 0U;
            bridge->ext_clock_tempo_valid = 0U;
            bridge->ext_clock_bpm_milli = 0U;
        }
    }

    bridge->ext_clock_last_tick = engine_ticks_now;
    runtime->ext_clock_tick_accum++;
    if (runtime->ext_clock_tick_accum >= SEQ_CLOCK_BRIDGE_MIDI_CLOCKS_PER_STEP)
    {
        runtime->ext_clock_tick_accum = 0U;
        *out_step_pulse = 1U;
    }
    return 1U;
}

void seq_clock_bridge_set_internal_tempo(seq_clock_bridge_t *bridge,
                                         seq_runtime_state_t *runtime,
                                         uint32_t bpm_milli)
{
    /* Hybrid seam: tempo change updates cadence policy and recomputes step timing; transport remains separate. */
    if ((bridge == 0) || (runtime == 0))
    {
        return;
    }

    bridge->tempo_bpm_milli = seq_clock_bridge_clamp_tempo(bpm_milli);
    seq_clock_bridge_internal_step_period_recompute(bridge, runtime);
    seq_clock_bridge_prepare_internal_run(bridge);
}

uint32_t seq_clock_bridge_get_internal_tempo_bpm_milli(const seq_clock_bridge_t *bridge)
{
    return (bridge != 0) ? bridge->tempo_bpm_milli : 0U;
}

uint8_t seq_clock_bridge_is_external_tempo_valid(const seq_clock_bridge_t *bridge)
{
    return (bridge != 0) ? bridge->ext_clock_tempo_valid : 0U;
}

uint32_t seq_clock_bridge_get_external_tempo_bpm_milli(const seq_clock_bridge_t *bridge)
{
    return (bridge != 0) ? bridge->ext_clock_bpm_milli : 0U;
}
