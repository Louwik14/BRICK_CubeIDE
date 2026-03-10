#include "Audio/recorder_transport.h"

#include "Core/engine_tasklet.h"

/**
 * @file recorder_transport.c
 * @brief Step-limited recorder transport driven by engine_tasklet ticks.
 *
 * Deterministic/IRQ-safety notes:
 * - This module never executes in the audio IRQ.
 * - It only reads engine_tick_count and updates small local state.
 * - No memory allocation, no blocking calls, constant-time processing per tick.
 *
 * Sequencer migration notes:
 * - This implementation is intentionally minimal and temporary.
 * - Sequencer transport will later provide tempo/grid-aware tick->step mapping,
 *   sequence-driven record triggers, and authoritative step progression.
 * - The live recorder audio functions remain the stable backend contract.
 */

#define RECORDER_TRANSPORT_DEFAULT_TICKS_PER_STEP (188U)

static recorder_transport_t g_transport;

static uint8_t recorder_transport_is_supported_step_length(uint32_t steps)
{
    return (uint8_t)((steps == 16U) ||
                     (steps == 32U) ||
                     (steps == 48U) ||
                     (steps == 64U));
}

void recorder_transport_init(void)
{
    g_transport.recording = 0U;
    g_transport.steps_recorded = 0U;
    g_transport.step_limit = 0U;
    g_transport.tick_counter = 0U;
    g_transport.ticks_per_step = RECORDER_TRANSPORT_DEFAULT_TICKS_PER_STEP;
    g_transport.last_tick_count = engine_tick_count;
}

void recorder_transport_start_record(uint32_t steps)
{
    if(recorder_transport_is_supported_step_length(steps) == 0U)
    {
        return;
    }

    g_transport.recording = 1U;
    g_transport.steps_recorded = 0U;
    g_transport.step_limit = steps;
    g_transport.tick_counter = 0U;
    g_transport.last_tick_count = engine_tick_count;
}

void recorder_transport_process(void)
{
    const uint32_t current_tick = engine_tick_count;

    if(g_transport.recording == 0U)
    {
        g_transport.last_tick_count = current_tick;
        return;
    }

    if(current_tick == g_transport.last_tick_count)
    {
        return;
    }

    const uint32_t elapsed_ticks = current_tick - g_transport.last_tick_count;
    g_transport.last_tick_count = current_tick;

    g_transport.tick_counter += elapsed_ticks;

    while((g_transport.tick_counter >= g_transport.ticks_per_step) &&
          (g_transport.recording != 0U))
    {
        g_transport.tick_counter -= g_transport.ticks_per_step;
        g_transport.steps_recorded++;

        if(g_transport.steps_recorded >= g_transport.step_limit)
        {
            g_transport.recording = 0U;
        }
    }
}

uint8_t recorder_transport_is_recording(void)
{
    return g_transport.recording;
}
