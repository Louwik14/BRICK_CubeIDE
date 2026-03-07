#include "Streaming/stream_manager.h"

#include <stdio.h>
#include <string.h>

#include "Storage/audio_streamer.h"
#include "stm32h7xx_hal.h"

#define STREAM_MANAGER_WATCHDOG_DT_MS (20U)
#define STREAM_MANAGER_INVALID_STREAMER_ID (0xFFU)

typedef struct
{
    volatile uint8_t active;
    volatile uint8_t stop_requested;
} stream_slot_t;

static stream_manager_stats_t g_stream_manager_stats;
static uint32_t g_last_process_tick;
static uint8_t g_process_tick_valid;
static stream_slot_t g_stream_slots[AUDIO_STREAMER_MAX_STREAMERS];
static uint32_t g_get_stream_frame_calls;

static void stream_manager_reset_slots(void)
{
    for(uint32_t i = 0U; i < AUDIO_STREAMER_MAX_STREAMERS; i++)
    {
        g_stream_slots[i].active = 0U;
        g_stream_slots[i].stop_requested = 0U;
        audio_streamer_stop((uint8_t)i);
    }
}

bool stream_manager_start(const char *path)
{
    memset(&g_stream_manager_stats, 0, sizeof(g_stream_manager_stats));
    g_last_process_tick = 0U;
    g_process_tick_valid = 0U;
    g_get_stream_frame_calls = 0U;
    stream_manager_reset_slots();

    if(path == NULL)
        return false;

    g_stream_slots[0].active = 1U;
    g_stream_slots[0].stop_requested = 0U;
    return audio_streamer_start(0U, path, 0U);
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

    for(uint32_t i = 0U; i < AUDIO_STREAMER_MAX_STREAMERS; i++)
    {
        if(g_stream_slots[i].stop_requested != 0U)
        {
            audio_streamer_stop((uint8_t)i);
            __DMB();
            g_stream_slots[i].stop_requested = 0U;
            continue;
        }

        if(g_stream_slots[i].active == 0U)
            continue;

        audio_streamer_process((uint8_t)i);

        if(audio_streamer_is_running((uint8_t)i) &&
           !audio_streamer_is_healthy((uint8_t)i))
        {
            g_stream_slots[i].active = 0U;
            __DMB();
            g_stream_slots[i].stop_requested = 1U;
        }
    }
}

void stream_manager_get_frame(float *L, float *R)
{
    audio_streamer_get_frame(0U, L, R);
}

bool stream_manager_start_stream(const sample_desc_t *sample_desc,
                                 uint32_t start_frame,
                                 uint8_t *out_streamer_id)
{
    printf("[STREAM] start request frame=%lu\r\n", (unsigned long)start_frame);

    if((sample_desc == NULL) || (sample_desc->valid == 0U) || (sample_desc->path[0] == '\0'))
    {
        printf("[STREAM] start rejected: invalid sample descriptor\r\n");
        return false;
    }

    if(start_frame >= sample_desc->length_frames)
    {
        printf("[STREAM] start rejected: start=%lu >= length=%lu\r\n",
               (unsigned long)start_frame,
               (unsigned long)sample_desc->length_frames);
        return false;
    }

    uint8_t slot_id = STREAM_MANAGER_INVALID_STREAMER_ID;

    for(uint32_t i = 0U; i < AUDIO_STREAMER_MAX_STREAMERS; i++)
    {
        if(g_stream_slots[i].active == 0U)
        {
            slot_id = (uint8_t)i;
            break;
        }
    }

    if(slot_id == STREAM_MANAGER_INVALID_STREAMER_ID)
    {
        printf("[STREAM] start rejected: no free slot\r\n");
        return false;
    }

    if(!audio_streamer_start(slot_id, sample_desc->path, start_frame))
    {
        printf("[STREAM] start rejected: audio_streamer_start failed (slot=%u)\r\n", (unsigned int)slot_id);
        return false;
    }

    g_stream_slots[slot_id].active = 1U;
    g_stream_slots[slot_id].stop_requested = 0U;

    if(out_streamer_id != NULL)
        *out_streamer_id = slot_id;

    printf("[STREAM] start id=%u path=%s frame=%lu\r\n",
           (unsigned int)slot_id,
           sample_desc->path,
           (unsigned long)start_frame);

    return true;
}

bool stream_manager_get_stream_frame(uint8_t streamer_id, float *L, float *R)
{
    if((L == NULL) || (R == NULL))
        return false;

    if((streamer_id >= AUDIO_STREAMER_MAX_STREAMERS) || (g_stream_slots[streamer_id].active == 0U))
        return false;

    if(!audio_streamer_is_healthy(streamer_id))
    {
        g_stream_slots[streamer_id].active = 0U;
        __DMB();
        g_stream_slots[streamer_id].stop_requested = 1U;
        return false;
    }

    audio_streamer_stats_t stats_before;
    audio_streamer_stats_t stats_after;
    memset(&stats_before, 0, sizeof(stats_before));
    memset(&stats_after, 0, sizeof(stats_after));

    audio_streamer_get_stats(streamer_id, &stats_before);
    audio_streamer_get_frame(streamer_id, L, R);
    audio_streamer_get_stats(streamer_id, &stats_after);
    g_get_stream_frame_calls++;

    if((g_get_stream_frame_calls <= 8U) || ((g_get_stream_frame_calls % 512U) == 0U))
    {
    	printf("[STREAM] get_frame id=%u calls=%lu ring=%lu\r\n",
    	       (unsigned int)streamer_id,
    	       (unsigned long)g_get_stream_frame_calls,
    	       (unsigned long)stats_after.ring_used_frames);
    }

    if(!audio_streamer_is_healthy(streamer_id))
    {
        g_stream_slots[streamer_id].active = 0U;
        __DMB();
        g_stream_slots[streamer_id].stop_requested = 1U;
        return false;
    }

    return (stats_after.underrun_count == stats_before.underrun_count);
}

void stream_manager_stop_stream(uint8_t streamer_id)
{
    if(streamer_id >= AUDIO_STREAMER_MAX_STREAMERS)
        return;

    g_stream_slots[streamer_id].active = 0U;
    __DMB();
    g_stream_slots[streamer_id].stop_requested = 1U;
}

void stream_manager_get_stats(stream_manager_stats_t *out_stats)
{
    if(out_stats == NULL)
        return;

    *out_stats = g_stream_manager_stats;
}

