#include "Core/brick6_stream_service_task.h"

#include <string.h>

#include "Core/brick6_sampler_runtime.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_time.h"
#include "Storage/sd_access_gate.h"
#include "stm32h7xx.h"

#define BRICK6_STREAM_SERVICE_BYTE_BUDGET (32768U)
#define BRICK6_STREAM_SERVICE_CADENCE_FRAMES (256U)

static volatile uint32_t g_brick6_stream_audio_wake_sequence;
static uint32_t g_brick6_stream_serviced_wake_sequence;
static sample_stream_audio_frame_t g_brick6_stream_last_service_frame;
static brick6_stream_service_task_stats_t g_brick6_stream_service_stats;

void brick6_stream_service_task_init(void)
{
    g_brick6_stream_audio_wake_sequence = 0U;
    g_brick6_stream_serviced_wake_sequence = 0U;
    g_brick6_stream_last_service_frame = sample_stream_time_now();
    memset(&g_brick6_stream_service_stats, 0, sizeof(g_brick6_stream_service_stats));
}

void brick6_stream_service_task_notify_audio_irq(void)
{
    g_brick6_stream_audio_wake_sequence++;
    __DMB();
}

void brick6_stream_service_task_poll(void)
{
    __DMB();
    const uint32_t requested_sequence = g_brick6_stream_audio_wake_sequence;
    const uint8_t pending = sample_stream_manager_has_pending_sd_work();
    if ((requested_sequence == g_brick6_stream_serviced_wake_sequence)
        && (pending == 0U))
    {
        return;
    }

    const sample_stream_audio_frame_t now = sample_stream_time_now();
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

    g_brick6_stream_service_stats.poll_count++;
    if ((multi_sample_load_has_pending() != 0U)
        || (sd_access_gate_bulk_exclusive_active() != 0U))
    {
        g_brick6_stream_service_stats.busy_poll_count++;
        return;
    }

    brick6_sampler_runtime_queue_stream_pages();
    sample_cache_service(BRICK6_STREAM_SERVICE_BYTE_BUDGET);
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
