#include "mixer_meter.h"

#define MIXER_METER_WINDOW_FRAMES 19200U

static volatile float g_pub_track_peak[SEQ_TRACK_COUNT];
static volatile float g_pub_master_peak = 0.0f;
static volatile uint32_t g_pub_master_clip_count = 0U;

static float g_acc_track_peak[SEQ_TRACK_COUNT];
static float g_acc_master_peak = 0.0f;
static uint32_t g_acc_master_clip_count = 0U;
static uint32_t g_acc_frames = 0U;
static volatile uint8_t g_reset_pending = 0U;

static inline float mixer_meter_maxf(float a, float b)
{
    return (a >= b) ? a : b;
}

static void mixer_meter_apply_reset_if_pending(void)
{
    if (g_reset_pending == 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_acc_track_peak[i] = 0.0f;
        g_pub_track_peak[i] = 0.0f;
    }

    g_acc_master_peak = 0.0f;
    g_acc_master_clip_count = 0U;
    g_acc_frames = 0U;

    g_pub_master_peak = 0.0f;
    g_pub_master_clip_count = 0U;
    g_reset_pending = 0U;
}

void mixer_meter_submit_track_peak(uint32_t track_id, float peak_abs)
{
    mixer_meter_apply_reset_if_pending();

    if (track_id >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_acc_track_peak[track_id] = mixer_meter_maxf(g_acc_track_peak[track_id], peak_abs);
}

void mixer_meter_submit_master_block(float peak_abs, uint32_t clip_count)
{
    mixer_meter_apply_reset_if_pending();
    g_acc_master_peak = mixer_meter_maxf(g_acc_master_peak, peak_abs);
    g_acc_master_clip_count += clip_count;
}

void mixer_meter_advance_window(uint32_t frames)
{
    mixer_meter_apply_reset_if_pending();

    g_acc_frames += frames;
    if (g_acc_frames < MIXER_METER_WINDOW_FRAMES)
    {
        return;
    }

    for (uint32_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        g_pub_track_peak[i] = g_acc_track_peak[i];
        g_acc_track_peak[i] = 0.0f;
    }

    g_pub_master_peak = g_acc_master_peak;
    g_pub_master_clip_count = g_acc_master_clip_count;

    g_acc_master_peak = 0.0f;
    g_acc_master_clip_count = 0U;
    g_acc_frames = 0U;
}

float mixer_meter_get_track_peak(uint32_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0.0f;
    }

    return g_pub_track_peak[track];
}

float mixer_meter_get_master_peak(void)
{
    return g_pub_master_peak;
}

uint32_t mixer_meter_get_master_clip_count(void)
{
    return g_pub_master_clip_count;
}

void mixer_meter_reset_window(void)
{
    g_reset_pending = 1U;
}

