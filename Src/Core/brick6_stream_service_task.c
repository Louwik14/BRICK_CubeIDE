#include "Core/brick6_stream_service_task.h"

#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_benchmark.h"
#include "Sampler/sample_stream_needs.h"
#include "Sampler/sample_stream_time.h"
#include "Sampler/sample_stream_underrun_trace.h"
#include "Storage/sd_access_gate.h"
#include "SD/sd_scheduler_runtime.h"
#include "stm32h7xx.h"

static volatile uint32_t g_brick6_stream_audio_wake_sequence;
static uint32_t g_brick6_stream_serviced_wake_sequence;
static sample_stream_audio_frame_t g_brick6_stream_last_service_frame;
static sample_stream_audio_frame_t g_brick6_stream_last_poll_frame;
static brick6_stream_service_task_stats_t g_brick6_stream_service_stats;

static void brick6_stream_service_task_update_gate(void)
{
    const uint32_t streaming =
        (sample_stream_manager_has_pending_sd_work() != 0U) ? 1U : 0U;
    if (streaming != g_brick6_stream_service_stats.streaming_active)
    {
        g_brick6_stream_service_stats.streaming_transition_count++;
    }
    g_brick6_stream_service_stats.streaming_active = streaming;
    sd_access_gate_set_streaming_critical((uint8_t)streaming);
}

void brick6_stream_service_task_init(void)
{
    g_brick6_stream_audio_wake_sequence = 0U;
    g_brick6_stream_serviced_wake_sequence = 0U;
    g_brick6_stream_last_service_frame = sample_stream_time_now();
    g_brick6_stream_last_poll_frame = g_brick6_stream_last_service_frame;
    memset(&g_brick6_stream_service_stats, 0, sizeof(g_brick6_stream_service_stats));
    brick6_stream_service_task_update_gate();
}

void brick6_stream_service_task_notify_audio_irq(void)
{
    g_brick6_stream_audio_wake_sequence++;
    __DMB();
}

void brick6_stream_service_task_poll(void)
{
    sd_scheduler_runtime_service();
    __DMB();
    const uint32_t requested_sequence = g_brick6_stream_audio_wake_sequence;
    brick6_stream_service_task_update_gate();
    const uint8_t pending = sample_cache_has_pending_sd_work();
    if ((requested_sequence == g_brick6_stream_serviced_wake_sequence)
        && (pending == 0U))
    {
        return;
    }

    const sample_stream_audio_frame_t now = sample_stream_time_now();
    const uint64_t poll_delay_64 = now - g_brick6_stream_last_poll_frame;
    const uint32_t poll_delay_frames = (poll_delay_64 > UINT32_MAX)
                                           ? UINT32_MAX : (uint32_t)poll_delay_64;
    g_brick6_stream_last_poll_frame = now;
    const uint64_t delay_64 = now - g_brick6_stream_last_service_frame;
    const uint32_t delay_frames = (delay_64 > UINT32_MAX) ? UINT32_MAX : (uint32_t)delay_64;
    if (delay_frames > g_brick6_stream_service_stats.max_dispatch_delay_frames)
    {
        g_brick6_stream_service_stats.max_dispatch_delay_frames = delay_frames;
    }
    if ((pending != 0U) && (delay_frames > BRICK6_STREAM_SERVICE_CADENCE_FRAMES))
    {
        g_brick6_stream_service_stats.cadence_miss_count++;
    }

    brick6_stream_underrun_trace_service_begin(
        BRICK6_STREAM_SERVICE_BYTE_BUDGET,
        sample_stream_needs_registry_count_active(),
        requested_sequence,
        delay_frames,
        (uint8_t)sd_access_gate_current_owner());

    g_brick6_stream_service_stats.poll_count++;
    if ((sample_stream_manager_io_in_flight() == 0U)
        && (multi_sample_load_is_active() != 0U))
    {
        sample_stream_manager_note_blocked_poll(
            multi_sample_load_is_active(),
            0U,
            poll_delay_frames);
        brick6_stream_underrun_trace_service_blocked(
            BRICK6_STREAM_TRACE_REASON_MULTI_LOAD_BLOCKED,
            (uint8_t)sd_access_gate_current_owner(),
            poll_delay_frames);
        brick6_stream_underrun_trace_service_end(
            BRICK6_STREAM_TRACE_REASON_MULTI_LOAD_BLOCKED,
            pending,
            g_brick6_stream_service_stats.streaming_active);
        g_brick6_stream_service_stats.busy_poll_count++;
#if BRICK6_STREAM_BENCH
        sample_stream_benchmark_note_blocked_poll();
#endif
        return;
    }

    brick6_sampler_runtime_queue_stream_pages();
    sample_cache_service(BRICK6_STREAM_SERVICE_BYTE_BUDGET);
    sd_scheduler_runtime_service();
    brick6_stream_service_task_update_gate();
    brick6_stream_underrun_trace_service_end(
        BRICK6_STREAM_TRACE_REASON_NONE,
        sample_stream_manager_has_pending_sd_work(),
        g_brick6_stream_service_stats.streaming_active);
    g_brick6_stream_serviced_wake_sequence = requested_sequence;
    g_brick6_stream_last_service_frame = sample_stream_time_now();
    g_brick6_stream_service_stats.audio_wake_sequence = requested_sequence;
    g_brick6_stream_service_stats.serviced_wake_sequence =
        g_brick6_stream_serviced_wake_sequence;
}

void brick6_stream_service_task_get_stats(brick6_stream_service_task_stats_t *out_stats)
{
    if (out_stats == 0)
    {
        return;
    }
    *out_stats = g_brick6_stream_service_stats;
    out_stats->audio_wake_sequence = g_brick6_stream_audio_wake_sequence;
    out_stats->serviced_wake_sequence = g_brick6_stream_serviced_wake_sequence;
}
