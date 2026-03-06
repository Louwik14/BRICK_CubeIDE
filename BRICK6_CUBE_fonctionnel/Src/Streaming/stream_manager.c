#include "Streaming/stream_manager.h"

#include <string.h>

#include "Storage/audio_streamer.h"
#include "stm32h7xx_hal.h"

#define STREAM_MANAGER_WATCHDOG_DT_MS (20U)

static stream_manager_stats_t g_stream_manager_stats;
static uint32_t g_last_process_tick;
static uint8_t g_process_tick_valid;

bool stream_manager_start(const char *path)
{
    memset(&g_stream_manager_stats, 0, sizeof(g_stream_manager_stats));
    g_last_process_tick = 0U;
    g_process_tick_valid = 0U;

    return audio_streamer_start(path);
}

void stream_manager_process(void)
{
    const uint32_t now = HAL_GetTick();

    g_stream_manager_stats.process_call_count++;

    if(g_process_tick_valid != 0U)
    {
        const uint32_t dt = now - g_last_process_tick;
        g_stream_manager_stats.process_dt_acc_ms += dt;
        g_stream_manager_stats.process_dt_samples++;

        if(dt > g_stream_manager_stats.process_dt_max_ms)
            g_stream_manager_stats.process_dt_max_ms = dt;

        if(dt > STREAM_MANAGER_WATCHDOG_DT_MS)
            g_stream_manager_stats.process_watchdog_count++;
    }

    g_last_process_tick = now;
    g_process_tick_valid = 1U;

    audio_streamer_process();
}

void stream_manager_get_frame(float *L, float *R)
{
    audio_streamer_get_frame(L, R);
}

void stream_manager_get_stats(stream_manager_stats_t *out_stats)
{
    if(out_stats == NULL)
        return;

    *out_stats = g_stream_manager_stats;
}
