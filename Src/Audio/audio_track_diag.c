#include "Audio/audio_track_diag.h"

#include <math.h>
#include <string.h>

#include "Audio/mixer.h"
#include "Seq/seq_types.h"
#include "stm32h7xx_hal.h"

#define AUDIO_TRACK_DIAG_WINDOW_FRAMES 12000U
#define AUDIO_TRACK_DIAG_HOLD_WINDOWS 8U

typedef struct
{
    float peak[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float energy[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float k_weighted_energy[AUDIO_TRACK_DIAG_STAGE_COUNT];
    float signed_sum[AUDIO_TRACK_DIAG_STAGE_COUNT];
    uint32_t samples[AUDIO_TRACK_DIAG_STAGE_COUNT];
    uint32_t window_frames;
} audio_track_diag_accum_t;

typedef struct
{
    float shelf_x1;
    float shelf_x2;
    float shelf_y1;
    float shelf_y2;
    float highpass_x1;
    float highpass_x2;
    float highpass_y1;
    float highpass_y2;
} audio_track_diag_k_state_t;

typedef struct
{
    volatile uint32_t sequence;
    audio_track_diag_snapshot_t value;
} audio_track_diag_slot_t;

typedef struct
{
    float peak_l[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float peak_r[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float energy_l[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    float energy_r[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t samples[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t nonfinite_count[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t over_full_scale_count[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
    uint32_t window_frames;
} audio_global_diag_accum_t;

typedef struct
{
    volatile uint32_t sequence;
    audio_global_diag_snapshot_t value;
} audio_global_diag_slot_t;

static volatile uint8_t g_diag_enabled;
static volatile uint8_t g_diag_logical_track;
static volatile uint8_t g_diag_mix_track;
static volatile uint8_t g_diag_filter_scope;
static uint8_t g_diag_soft_clip_available;
static uint8_t g_diag_lane_active;
static uint8_t g_diag_filter_active;
static audio_track_diag_accum_t g_diag_accum;
static audio_track_diag_k_state_t g_diag_k_state;
static float g_diag_peak_hold[SEQ_TRACK_COUNT][AUDIO_TRACK_DIAG_STAGE_COUNT];
static uint8_t g_diag_hold_age[SEQ_TRACK_COUNT][AUDIO_TRACK_DIAG_STAGE_COUNT];
static volatile uint32_t g_diag_soft_clips[SEQ_TRACK_COUNT];
static volatile uint32_t g_diag_filter_clips[SEQ_TRACK_COUNT];
static volatile uint32_t g_diag_insert_clips[SEQ_TRACK_COUNT];
static audio_track_diag_slot_t g_diag_slots[SEQ_TRACK_COUNT];
static audio_global_diag_accum_t g_global_accum;
static audio_global_diag_slot_t g_global_slot;
static uint8_t g_global_stage_state[AUDIO_GLOBAL_DIAG_STAGE_COUNT];
static uint8_t g_global_active_tracks;
static volatile uint32_t g_global_final_clips;
static float g_global_final_clip_max_over;
static volatile uint32_t g_global_master_fx_clamps;
static float g_global_master_fx_clamp_max_over;
static volatile uint32_t g_global_delay_clamps;
static float g_global_delay_clamp_max_over;

static float audio_track_diag_abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

static void audio_track_diag_clear_accum(void)
{
    memset(&g_diag_accum, 0, sizeof(g_diag_accum));
    memset(&g_diag_k_state, 0, sizeof(g_diag_k_state));
}

/*
 * ITU-R BS.1770 K-weighting at 48 kHz.  This diagnostic-only cascade is
 * stateful and observes a copy of the selected engine signal; it never feeds
 * the mixer.  The first biquad is the high-frequency shelf and the second is
 * the RLB high-pass stage.
 */
static float audio_track_diag_k_weight(float input)
{
    const float shelf =
        (1.53512486f * input)
        + (-2.69169619f * g_diag_k_state.shelf_x1)
        + (1.19839281f * g_diag_k_state.shelf_x2)
        - (-1.69065929f * g_diag_k_state.shelf_y1)
        - (0.73248077f * g_diag_k_state.shelf_y2);
    g_diag_k_state.shelf_x2 = g_diag_k_state.shelf_x1;
    g_diag_k_state.shelf_x1 = input;
    g_diag_k_state.shelf_y2 = g_diag_k_state.shelf_y1;
    g_diag_k_state.shelf_y1 = shelf;

    const float highpass =
        shelf
        + (-2.0f * g_diag_k_state.highpass_x1)
        + g_diag_k_state.highpass_x2
        - (-1.99004745f * g_diag_k_state.highpass_y1)
        - (0.99007225f * g_diag_k_state.highpass_y2);
    g_diag_k_state.highpass_x2 = g_diag_k_state.highpass_x1;
    g_diag_k_state.highpass_x1 = shelf;
    g_diag_k_state.highpass_y2 = g_diag_k_state.highpass_y1;
    g_diag_k_state.highpass_y1 = highpass;
    return highpass;
}

static void audio_global_diag_clear_accum(void)
{
    memset(&g_global_accum, 0, sizeof(g_global_accum));
}

void audio_track_diag_select(uint8_t logical_track, uint8_t mix_track, uint8_t soft_clip_available)
{
    if ((logical_track >= SEQ_TRACK_COUNT) || (mix_track >= MIXER_MAX_TRACKS))
    {
        g_diag_enabled = 0U;
        return;
    }
    g_diag_enabled = 0U;
    g_diag_logical_track = logical_track;
    g_diag_mix_track = mix_track;
    g_diag_soft_clip_available = soft_clip_available;
    g_diag_filter_scope = 0U;
    audio_track_diag_clear_accum();
    g_diag_enabled = 1U;
}

void audio_track_diag_open(uint8_t logical_track, uint8_t mix_track, uint8_t soft_clip_available)
{
    audio_track_diag_select(logical_track, mix_track, soft_clip_available);
}

void audio_track_diag_close(void)
{
    g_diag_enabled = 0U;
    g_diag_filter_scope = 0U;
}

uint8_t audio_track_diag_is_enabled(void)
{
    return g_diag_enabled;
}

uint8_t audio_track_diag_is_selected_mix_track(uint8_t mix_track)
{
    return ((g_diag_enabled != 0U) && (mix_track == g_diag_mix_track)) ? 1U : 0U;
}

uint8_t audio_track_diag_is_selected_logical_track(uint8_t logical_track)
{
    return ((g_diag_enabled != 0U) && (logical_track == g_diag_logical_track)) ? 1U : 0U;
}

void audio_track_diag_set_lane_active(uint8_t active)
{
    g_diag_lane_active = (active != 0U) ? 1U : 0U;
}

void audio_track_diag_set_filter_active(uint8_t active)
{
    g_diag_filter_active = (active != 0U) ? 1U : 0U;
}

void audio_track_diag_measure_sample(audio_track_diag_stage_t stage, float left, float right)
{
    if ((g_diag_enabled == 0U) || (stage >= AUDIO_TRACK_DIAG_STAGE_COUNT))
    {
        return;
    }
    const float abs_l = audio_track_diag_abs(left);
    const float abs_r = audio_track_diag_abs(right);
    const float peak = (abs_l > abs_r) ? abs_l : abs_r;
    if (peak > g_diag_accum.peak[stage])
    {
        g_diag_accum.peak[stage] = peak;
    }
    g_diag_accum.energy[stage] += 0.5f * ((left * left) + (right * right));
    g_diag_accum.signed_sum[stage] += 0.5f * (left + right);
    if (stage == AUDIO_TRACK_DIAG_ENG)
    {
        const float weighted = audio_track_diag_k_weight(0.5f * (left + right));
        g_diag_accum.k_weighted_energy[stage] += weighted * weighted;
    }
    g_diag_accum.samples[stage]++;
}

static void audio_global_diag_accumulate_sample(audio_global_diag_stage_t stage,
                                                float left,
                                                float right)
{
    if ((!isfinite(left)) || (!isfinite(right)))
    {
        g_global_accum.nonfinite_count[stage]++;
        return;
    }
    const float abs_l = audio_track_diag_abs(left);
    const float abs_r = audio_track_diag_abs(right);
    if ((abs_l > 1.0f) || (abs_r > 1.0f))
    {
        g_global_accum.over_full_scale_count[stage]++;
    }
    if (abs_l > g_global_accum.peak_l[stage])
    {
        g_global_accum.peak_l[stage] = abs_l;
    }
    if (abs_r > g_global_accum.peak_r[stage])
    {
        g_global_accum.peak_r[stage] = abs_r;
    }
    g_global_accum.energy_l[stage] += left * left;
    g_global_accum.energy_r[stage] += right * right;
    g_global_accum.samples[stage]++;
}

void audio_global_diag_measure_sample(audio_global_diag_stage_t stage,
                                      float left,
                                      float right)
{
    if ((g_diag_enabled == 0U) || (stage >= AUDIO_GLOBAL_DIAG_STAGE_COUNT))
    {
        return;
    }
    g_global_stage_state[stage] = (uint8_t)AUDIO_GLOBAL_DIAG_STATE_MEASURED;
    audio_global_diag_accumulate_sample(stage, left, right);
}

void audio_global_diag_set_active_tracks(uint8_t count)
{
    if (g_diag_enabled != 0U)
    {
        g_global_active_tracks = count;
    }
}

void audio_global_diag_set_stage_state(audio_global_diag_stage_t stage,
                                       audio_global_diag_state_t state)
{
    if ((g_diag_enabled != 0U) && (stage < AUDIO_GLOBAL_DIAG_STAGE_COUNT))
    {
        g_global_stage_state[stage] = (uint8_t)state;
    }
}

void audio_global_diag_measure_stereo(audio_global_diag_stage_t stage,
                                      const float *left,
                                      const float *right,
                                      uint32_t frames)
{
    if ((g_diag_enabled == 0U) || (stage >= AUDIO_GLOBAL_DIAG_STAGE_COUNT)
        || (left == 0) || (right == 0))
    {
        return;
    }
    g_global_stage_state[stage] = (uint8_t)AUDIO_GLOBAL_DIAG_STATE_MEASURED;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        audio_global_diag_accumulate_sample(stage, left[i], right[i]);
    }
}

void audio_global_diag_measure_three(audio_global_diag_stage_t stage1,
                                     const float *left1,
                                     const float *right1,
                                     audio_global_diag_stage_t stage2,
                                     const float *left2,
                                     const float *right2,
                                     audio_global_diag_stage_t stage3,
                                     const float *left3,
                                     const float *right3,
                                     uint32_t frames)
{
    if ((g_diag_enabled == 0U)
        || (stage1 >= AUDIO_GLOBAL_DIAG_STAGE_COUNT)
        || (stage2 >= AUDIO_GLOBAL_DIAG_STAGE_COUNT)
        || (stage3 >= AUDIO_GLOBAL_DIAG_STAGE_COUNT)
        || (left1 == 0) || (right1 == 0)
        || (left2 == 0) || (right2 == 0)
        || (left3 == 0) || (right3 == 0))
    {
        return;
    }
    g_global_stage_state[stage1] = (uint8_t)AUDIO_GLOBAL_DIAG_STATE_MEASURED;
    g_global_stage_state[stage2] = (uint8_t)AUDIO_GLOBAL_DIAG_STATE_MEASURED;
    g_global_stage_state[stage3] = (uint8_t)AUDIO_GLOBAL_DIAG_STATE_MEASURED;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        audio_global_diag_accumulate_sample(stage1, left1[i], right1[i]);
        audio_global_diag_accumulate_sample(stage2, left2[i], right2[i]);
        audio_global_diag_accumulate_sample(stage3, left3[i], right3[i]);
    }
}

static void audio_global_diag_report_clamp(float input,
                                           float clipped,
                                           volatile uint32_t *count,
                                           float *max_over)
{
    if ((g_diag_enabled == 0U) || (input == clipped))
    {
        return;
    }
    (*count)++;
    const float over = audio_track_diag_abs(input - clipped);
    if (over > *max_over)
    {
        *max_over = over;
    }
}

void audio_global_diag_report_final_pcm24(float input, float clipped)
{
    audio_global_diag_report_clamp(input, clipped, &g_global_final_clips,
                                   &g_global_final_clip_max_over);
}

void audio_global_diag_report_master_fx_clamp(float input, float clipped)
{
    audio_global_diag_report_clamp(input, clipped, &g_global_master_fx_clamps,
                                   &g_global_master_fx_clamp_max_over);
}

void audio_global_diag_report_delay_clamp(float input, float clipped)
{
    audio_global_diag_report_clamp(input, clipped, &g_global_delay_clamps,
                                   &g_global_delay_clamp_max_over);
}

static void audio_global_diag_publish(void)
{
    g_global_slot.sequence++;
    for (uint8_t stage = 0U; stage < AUDIO_GLOBAL_DIAG_STAGE_COUNT; ++stage)
    {
        g_global_slot.value.peak_l[stage] = g_global_accum.peak_l[stage];
        g_global_slot.value.peak_r[stage] = g_global_accum.peak_r[stage];
        g_global_slot.value.energy_l[stage] = g_global_accum.energy_l[stage];
        g_global_slot.value.energy_r[stage] = g_global_accum.energy_r[stage];
        g_global_slot.value.samples[stage] = g_global_accum.samples[stage];
        g_global_slot.value.nonfinite_count[stage] =
            g_global_accum.nonfinite_count[stage];
        g_global_slot.value.over_full_scale_count[stage] =
            g_global_accum.over_full_scale_count[stage];
        g_global_slot.value.state[stage] = g_global_stage_state[stage];
    }
    g_global_slot.value.active_audio_tracks = g_global_active_tracks;
    g_global_slot.value.final_clip_count = g_global_final_clips;
    g_global_slot.value.final_clip_max_over = g_global_final_clip_max_over;
    g_global_slot.value.master_fx_clamp_count = g_global_master_fx_clamps;
    g_global_slot.value.master_fx_clamp_max_over = g_global_master_fx_clamp_max_over;
    g_global_slot.value.delay_clamp_count = g_global_delay_clamps;
    g_global_slot.value.delay_clamp_max_over = g_global_delay_clamp_max_over;
    g_global_slot.value.delay_clamp_available = 1U;
    g_global_slot.value.reverb_clamp_available = 0U;
    g_global_slot.value.master_fx_clamp_available = 1U;
    g_global_slot.value.final_clip_available = 1U;
    g_global_slot.sequence++;
}

void audio_global_diag_end_block(uint32_t frames)
{
    if (g_diag_enabled == 0U)
    {
        return;
    }
    g_global_accum.window_frames += frames;
}

void audio_track_diag_measure_stereo(audio_track_diag_stage_t stage,
                                     const float *left,
                                     const float *right,
                                     uint32_t frames)
{
    if ((left == 0) || (right == 0))
    {
        return;
    }
    for (uint32_t i = 0U; i < frames; ++i)
    {
        audio_track_diag_measure_sample(stage, left[i], right[i]);
    }
}

void audio_track_diag_measure_mono(audio_track_diag_stage_t stage,
                                   const float *mono,
                                   uint32_t frames)
{
    if (mono == 0)
    {
        return;
    }
    for (uint32_t i = 0U; i < frames; ++i)
    {
        audio_track_diag_measure_sample(stage, mono[i], mono[i]);
    }
}

void audio_track_diag_filter_scope(uint8_t enabled)
{
    g_diag_filter_scope = ((g_diag_enabled != 0U) && (enabled != 0U)) ? 1U : 0U;
}

void audio_track_diag_report_filter_clip(void)
{
    if ((g_diag_filter_scope != 0U) && (g_diag_logical_track < SEQ_TRACK_COUNT))
    {
        g_diag_filter_clips[g_diag_logical_track]++;
    }
}

void audio_track_diag_report_stack_soft_clips(uint8_t logical_track, uint32_t count)
{
    if ((count != 0U) && (audio_track_diag_is_selected_logical_track(logical_track) != 0U))
    {
        g_diag_soft_clips[logical_track] += count;
    }
}

void audio_track_diag_report_insert_clip(uint8_t logical_track)
{
    if (audio_track_diag_is_selected_logical_track(logical_track) != 0U)
    {
        g_diag_insert_clips[logical_track]++;
    }
}

static void audio_track_diag_publish(void)
{
    const uint8_t track = g_diag_logical_track;
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    audio_track_diag_slot_t *const slot = &g_diag_slots[track];
    slot->sequence++;
    for (uint8_t stage = 0U; stage < AUDIO_TRACK_DIAG_STAGE_COUNT; ++stage)
    {
        const float window_peak = g_diag_accum.peak[stage];
        if (window_peak >= g_diag_peak_hold[track][stage])
        {
            g_diag_peak_hold[track][stage] = window_peak;
            g_diag_hold_age[track][stage] = 0U;
        }
        else if (++g_diag_hold_age[track][stage] >= AUDIO_TRACK_DIAG_HOLD_WINDOWS)
        {
            g_diag_peak_hold[track][stage] = window_peak;
            g_diag_hold_age[track][stage] = 0U;
        }
        slot->value.peak[stage] = g_diag_peak_hold[track][stage];
        slot->value.rms_energy[stage] = (float)g_diag_accum.energy[stage];
        slot->value.k_weighted_energy[stage] =
            g_diag_accum.k_weighted_energy[stage];
        slot->value.signed_sum[stage] = g_diag_accum.signed_sum[stage];
        slot->value.samples[stage] = g_diag_accum.samples[stage];
    }
    slot->value.soft_clip_count = g_diag_soft_clips[track];
    slot->value.filter_clip_count = g_diag_filter_clips[track];
    slot->value.insert_clip_count = g_diag_insert_clips[track];
    slot->value.active = g_diag_lane_active;
    slot->value.filter_active = g_diag_filter_active;
    slot->value.soft_clip_available = g_diag_soft_clip_available;
    slot->sequence++;
}

void audio_track_diag_end_block(uint32_t frames)
{
    if (g_diag_enabled == 0U)
    {
        return;
    }
    g_diag_accum.window_frames += frames;
}

void audio_track_diag_reset(uint8_t logical_track)
{
    if (logical_track >= SEQ_TRACK_COUNT)
    {
        return;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_diag_soft_clips[logical_track] = 0U;
    g_diag_filter_clips[logical_track] = 0U;
    g_diag_insert_clips[logical_track] = 0U;
    memset(g_diag_peak_hold[logical_track], 0, sizeof(g_diag_peak_hold[logical_track]));
    memset(g_diag_hold_age[logical_track], 0, sizeof(g_diag_hold_age[logical_track]));
    memset(&g_diag_slots[logical_track], 0, sizeof(g_diag_slots[logical_track]));
    if (logical_track == g_diag_logical_track)
    {
        audio_track_diag_clear_accum();
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void audio_track_diag_reset_all(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        audio_track_diag_reset(track);
    }
}

void audio_global_diag_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    audio_global_diag_clear_accum();
    memset(&g_global_slot, 0, sizeof(g_global_slot));
    memset(g_global_stage_state, 0, sizeof(g_global_stage_state));
    g_global_active_tracks = 0U;
    g_global_final_clips = 0U;
    g_global_final_clip_max_over = 0.0f;
    g_global_master_fx_clamps = 0U;
    g_global_master_fx_clamp_max_over = 0.0f;
    g_global_delay_clamps = 0U;
    g_global_delay_clamp_max_over = 0.0f;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t audio_track_diag_read(uint8_t logical_track, audio_track_diag_snapshot_t *out)
{
    if ((logical_track >= SEQ_TRACK_COUNT) || (out == 0))
    {
        return 0U;
    }
    const audio_track_diag_slot_t *const slot = &g_diag_slots[logical_track];
    for (;;)
    {
        const uint32_t before = slot->sequence;
        if ((before & 1U) != 0U)
        {
            continue;
        }
        *out = slot->value;
        const uint32_t after = slot->sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            return 1U;
        }
    }
}

uint8_t audio_track_diag_read_coherent(uint8_t logical_track,
                                       audio_track_diag_snapshot_t *track,
                                       audio_global_diag_snapshot_t *global)
{
    if ((logical_track >= SEQ_TRACK_COUNT) || (track == 0) || (global == 0))
    {
        return 0U;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    audio_track_diag_publish();
    audio_global_diag_publish();
    *track = g_diag_slots[logical_track].value;
    *global = g_global_slot.value;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return 1U;
}
