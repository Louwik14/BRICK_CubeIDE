#include "Core/live_parameter_audio_queue.h"

#include "Core/live_clock.h"
#include "Core/live_parameter_event.h"
#include "memory_layout.h"
#include "Seq/seq_runtime_exec.h"
#include "stm32h7xx_hal.h"

#define LIVE_PARAMETER_AUDIO_QUEUE_MASK (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)

SEQ_STATE_D2 static live_parameter_audio_event_t
    g_live_parameter_audio_scheduled[LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY];
SEQ_STATE_D2 static live_parameter_audio_event_t
    g_live_parameter_audio_due[LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY];
static volatile uint16_t g_live_parameter_audio_scheduled_count;
static volatile uint16_t g_live_parameter_audio_due_head;
static volatile uint16_t g_live_parameter_audio_due_tail;
static volatile uint16_t g_live_parameter_audio_due_count;
static live_parameter_audio_queue_diag_t g_live_parameter_audio_diag;

static uint32_t live_parameter_audio_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return primask;
}

static void live_parameter_audio_exit_critical(uint32_t primask)
{
    __DMB();
    __set_PRIMASK(primask);
}

static uint8_t live_parameter_audio_same_target(
    const live_parameter_audio_event_t *left,
    const live_parameter_audio_event_t *right)
{
    return (uint8_t)((left->parameter_id == right->parameter_id)
                     && (left->source == right->source)
                     && (left->scope == right->scope)
                     && (left->track == right->track)
                     && (left->slot == right->slot));
}

static uint8_t live_parameter_audio_schedule(
    const live_parameter_audio_event_t *event)
{
    const uint32_t primask = live_parameter_audio_enter_critical();
    uint16_t count = g_live_parameter_audio_scheduled_count;

    if (count >= LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY)
    {
        /* Coalescing is a saturation-only escape hatch.  It is valid only for
         * successive updates of the same target that are still unapplied. */
        if ((count != 0U)
                && (live_parameter_audio_same_target(
                        &g_live_parameter_audio_scheduled[count - 1U], event) != 0U)
                && (g_live_parameter_audio_scheduled[count - 1U].ingress_serial
                    < event->ingress_serial))
        {
            g_live_parameter_audio_scheduled[count - 1U] = *event;
            g_live_parameter_audio_diag.coalesced_count++;
            live_parameter_audio_exit_critical(primask);
            return 1U;
        }

        g_live_parameter_audio_diag.queue_drop_count++;
        live_parameter_audio_exit_critical(primask);
        return 0U;
    }

    uint16_t index = count;
    while ((index != 0U)
           && ((g_live_parameter_audio_scheduled[index - 1U].effective_sample_time
                > event->effective_sample_time)
               || ((g_live_parameter_audio_scheduled[index - 1U].effective_sample_time
                    == event->effective_sample_time)
                   && (g_live_parameter_audio_scheduled[index - 1U].ingress_serial
                       > event->ingress_serial))))
    {
        g_live_parameter_audio_scheduled[index] =
            g_live_parameter_audio_scheduled[index - 1U];
        --index;
    }

    g_live_parameter_audio_scheduled[index] = *event;
    ++count;
    g_live_parameter_audio_scheduled_count = count;
    if (count > g_live_parameter_audio_diag.high_water)
        g_live_parameter_audio_diag.high_water = count;

    live_parameter_audio_exit_critical(primask);
    return 1U;
}

void live_parameter_audio_queue_init(void)
{
    const uint32_t primask = live_parameter_audio_enter_critical();

    g_live_parameter_audio_scheduled_count = 0U;
    g_live_parameter_audio_due_head = 0U;
    g_live_parameter_audio_due_tail = 0U;
    g_live_parameter_audio_due_count = 0U;
    g_live_parameter_audio_diag = (live_parameter_audio_queue_diag_t){ 0 };

    live_parameter_audio_exit_critical(primask);
}

uint16_t live_parameter_audio_queue_drain(void)
{
    uint16_t drained = 0U;

    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_DRAIN_BUDGET; ++i)
    {
        live_parameter_event_t control_event;
        if (live_parameter_event_pop(&control_event) == 0U)
            break;

        uint64_t effective_sample_time = 0U;
        if (live_clock_tim5_to_guarded_sample_time(control_event.capture_tick,
                                                   &effective_sample_time) == 0U)
        {
            const uint32_t primask = live_parameter_audio_enter_critical();
            g_live_parameter_audio_diag.conversion_drop_count++;
            live_parameter_audio_exit_critical(primask);
            continue;
        }

        const uint64_t now = seq_runtime_exec_get_audio_timeline_sample();
        if (effective_sample_time < now)
        {
            const uint64_t lateness = now - effective_sample_time;
            const uint32_t primask = live_parameter_audio_enter_critical();
            g_live_parameter_audio_diag.late_count++;
            if (lateness > g_live_parameter_audio_diag.max_lateness_samples)
                g_live_parameter_audio_diag.max_lateness_samples = lateness;
            if (lateness > LIVE_PARAMETER_AUDIO_STALE_THRESHOLD_SAMPLES)
                g_live_parameter_audio_diag.stale_count++;
            live_parameter_audio_exit_critical(primask);
            effective_sample_time = now;
        }

        const live_parameter_audio_event_t audio_event = {
            .effective_sample_time = effective_sample_time,
            .capture_tick = control_event.capture_tick,
            .ingress_serial = control_event.ingress_serial,
            .parameter_id = control_event.parameter_id,
            .source = control_event.source,
            .scope = control_event.scope,
            .track = control_event.track,
            .slot = control_event.slot,
            .flags = control_event.flags,
            .value = control_event.value
        };
        if (live_parameter_audio_schedule(&audio_event) != 0U)
            ++drained;
    }

    return drained;
}

uint16_t live_parameter_audio_queue_frames_until_deadline(uint64_t block_start,
                                                          uint16_t max_frames)
{
    const uint32_t primask = live_parameter_audio_enter_critical();
    const uint16_t count = g_live_parameter_audio_scheduled_count;
    const uint64_t deadline = (count != 0U)
        ? g_live_parameter_audio_scheduled[0].effective_sample_time : 0U;
    live_parameter_audio_exit_critical(primask);

    if ((count == 0U) || (max_frames == 0U))
        return max_frames;
    if (deadline <= block_start)
        return max_frames;

    const uint64_t until_deadline = deadline - block_start;
    if (until_deadline >= (uint64_t)max_frames)
        return max_frames;
    return (uint16_t)until_deadline;
}

uint16_t live_parameter_audio_queue_consume_due(uint64_t now)
{
    const uint32_t primask = live_parameter_audio_enter_critical();
    uint16_t consumed = 0U;

    while ((g_live_parameter_audio_scheduled_count != 0U)
           && (g_live_parameter_audio_scheduled[0].effective_sample_time <= now))
    {
        if (g_live_parameter_audio_due_count >= LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY)
        {
            g_live_parameter_audio_diag.due_drop_count++;
            break;
        }

        const uint16_t due_index =
            (uint16_t)(g_live_parameter_audio_due_head & LIVE_PARAMETER_AUDIO_QUEUE_MASK);
        g_live_parameter_audio_due[due_index] = g_live_parameter_audio_scheduled[0];
        g_live_parameter_audio_due_head = (uint16_t)(g_live_parameter_audio_due_head + 1U);
        g_live_parameter_audio_due_count++;
        if (g_live_parameter_audio_due_count > g_live_parameter_audio_diag.due_high_water)
            g_live_parameter_audio_diag.due_high_water = g_live_parameter_audio_due_count;

        --g_live_parameter_audio_scheduled_count;
        for (uint16_t index = 0U;
             index < g_live_parameter_audio_scheduled_count;
             ++index)
        {
            g_live_parameter_audio_scheduled[index] =
                g_live_parameter_audio_scheduled[index + 1U];
        }
        ++consumed;
    }

    live_parameter_audio_exit_critical(primask);
    return consumed;
}

bool live_parameter_audio_queue_pop_due(live_parameter_audio_event_t *out_event)
{
    if (out_event == 0)
        return false;

    const uint32_t primask = live_parameter_audio_enter_critical();
    if (g_live_parameter_audio_due_count == 0U)
    {
        live_parameter_audio_exit_critical(primask);
        return false;
    }

    const uint16_t due_index =
        (uint16_t)(g_live_parameter_audio_due_tail & LIVE_PARAMETER_AUDIO_QUEUE_MASK);
    *out_event = g_live_parameter_audio_due[due_index];
    g_live_parameter_audio_due_tail = (uint16_t)(g_live_parameter_audio_due_tail + 1U);
    --g_live_parameter_audio_due_count;

    live_parameter_audio_exit_critical(primask);
    return true;
}

uint16_t live_parameter_audio_queue_scheduled_depth(void)
{
    const uint32_t primask = live_parameter_audio_enter_critical();
    const uint16_t depth = g_live_parameter_audio_scheduled_count;
    live_parameter_audio_exit_critical(primask);
    return depth;
}

uint16_t live_parameter_audio_queue_due_depth(void)
{
    const uint32_t primask = live_parameter_audio_enter_critical();
    const uint16_t depth = g_live_parameter_audio_due_count;
    live_parameter_audio_exit_critical(primask);
    return depth;
}

void live_parameter_audio_queue_get_diag(live_parameter_audio_queue_diag_t *out_diag)
{
    if (out_diag == 0)
        return;

    const uint32_t primask = live_parameter_audio_enter_critical();
    *out_diag = g_live_parameter_audio_diag;
    out_diag->scheduled_depth = g_live_parameter_audio_scheduled_count;
    out_diag->due_depth = g_live_parameter_audio_due_count;
    live_parameter_audio_exit_critical(primask);
}
