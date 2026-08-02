#include "fx_master_macro.h"

#include <math.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Audio/audio_track_diag.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "audio_float.h"
#include "memory_layout.h"
#include "Audio/fx_chorus_bench.h"

#define FX_MASTER_MACRO_SLOT_COUNT 4U
#define FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT 48000.0f
#define FX_MASTER_MACRO_SMOOTH_FAST 0.08f
#define FX_MASTER_MACRO_SMOOTH_GAIN 0.12f
#define FX_MASTER_MACRO_DC_ALPHA 0.995f
#define FX_MASTER_MACRO_DELAY_MAX_SAMPLES 48000U
#define FX_MASTER_MACRO_DELAY_MAX_F ((float)(FX_MASTER_MACRO_DELAY_MAX_SAMPLES - 2U))
#define FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES 24000U
#define FX_MASTER_MACRO_STUTTER_HISTORY_F ((float)(FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES - 2U))
#define FX_MASTER_MACRO_STUTTER_OWNER_NONE 0xFFU
#define FX_MASTER_MACRO_MUTE_FADE_SECONDS 0.005f

typedef struct
{
    uint8_t last_type;
    float wet;
    float ring_phase;
    float chop_phase;
    float pump_phase;
    float chop_gain;
    float pump_gain;
    float delay_samples;
    float delay_feedback_lp;
    float wobble_phase;
    float freeze_gate;
    float stutter_pos;
    float stutter_start;
    float stutter_loop_samples;
    uint8_t stutter_active;
    float color_smooth_l;
    float color_smooth_r;
    float color_amount;
    float color_coeff;
    float crush_hold_l;
    float crush_hold_r;
    uint8_t color_init;
    uint16_t crush_count;
    uint32_t delay_write;
    uint32_t delay_filled;
    float dc_x_l;
    float dc_y_l;
    float dc_x_r;
    float dc_y_r;
} fx_master_macro_slot_state_t;

typedef struct
{
    uint8_t owner_slot;
    uint8_t playing;
    uint8_t relatch_pending;
    uint8_t xfade_active;
    uint32_t write;
    uint32_t filled;
    float pos;
    float start;
    float loop_samples;
    float pending_loop_samples;
    float old_pos;
    float old_start;
    float old_loop_samples;
    float xfade_pos;
} fx_master_macro_stutter_state_t;

AUDIO_HOT static fx_master_macro_slot_state_t g_slots[FX_MASTER_MACRO_SLOT_COUNT];
AUDIO_HOT static fx_master_macro_stutter_state_t g_stutter;
AUDIO_COLD_SDRAM static float g_delay[FX_MASTER_MACRO_SLOT_COUNT][FX_MASTER_MACRO_DELAY_MAX_SAMPLES];
AUDIO_HISTORY_SDRAM static float g_stutter_history_l[FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES];
AUDIO_HISTORY_SDRAM static float g_stutter_history_r[FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES];
static float g_sample_rate = FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT;
static uint8_t g_fxmm_diag_enabled;
static uint8_t g_fxmm_muted;
static float g_fxmm_mute_gain = 1.0f;
static float g_fxmm_dry_l[AUDIO_BLOCK_SIZE];
static float g_fxmm_dry_r[AUDIO_BLOCK_SIZE];

static float fxmm_clampf(float v, float lo, float hi)
{
    if (!(v >= lo))
    {
        return lo;
    }
    if (!(v <= hi))
    {
        return hi;
    }
    return v;
}

static float fxmm_audio_clampf(float v, float lo, float hi)
{
    const float clipped = fxmm_clampf(v, lo, hi);
    if (g_fxmm_diag_enabled != 0U)
    {
        audio_global_diag_report_macro_fx_clamp(v, clipped);
    }
    return clipped;
}

static uint8_t fxmm_u7(float v)
{
    const float clamped = fxmm_clampf(v, 0.0f, 127.0f);
    return (uint8_t)(clamped + 0.5f);
}

static float fxmm_norm(float v)
{
    return fxmm_clampf(v, 0.0f, 127.0f) * (1.0f / 127.0f);
}

static float fxmm_smooth(float current, float target, float coeff)
{
    return current + ((target - current) * coeff);
}

static uint8_t fxmm_type_is_active(uint8_t type)
{
    switch (type)
    {
        case FX_MASTER_MACRO_DRIVE:
        case FX_MASTER_MACRO_CRUSH:
        case FX_MASTER_MACRO_WOBBLE:
        case FX_MASTER_MACRO_COMB:
        case FX_MASTER_MACRO_RING:
        case FX_MASTER_MACRO_CHOP:
        case FX_MASTER_MACRO_PUMP:
        case FX_MASTER_MACRO_STUTTER:
        case FX_MASTER_MACRO_FREEZE:
        case FX_MASTER_MACRO_COLOR:
        case FX_MASTER_MACRO_CHORUS_MICRO:
        case FX_MASTER_MACRO_CHORUS_DAISY:
        case FX_MASTER_MACRO_CHORUS_JUNO:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t fxmm_type_is_unique_history(uint8_t type)
{
    return (type == FX_MASTER_MACRO_STUTTER) ? 1U : 0U;
}

static uint8_t fxmm_type_is_unique_freeze(uint8_t type)
{
    return (type == FX_MASTER_MACRO_FREEZE) ? 1U : 0U;
}

static void fxmm_reset_stutter_state(void)
{
    g_stutter.owner_slot = FX_MASTER_MACRO_STUTTER_OWNER_NONE;
    g_stutter.playing = 0U;
    g_stutter.relatch_pending = 0U;
    g_stutter.xfade_active = 0U;
    g_stutter.write = 0U;
    g_stutter.filled = 0U;
    g_stutter.pos = 0.0f;
    g_stutter.start = 0.0f;
    g_stutter.loop_samples = 1200.0f;
    g_stutter.pending_loop_samples = 1200.0f;
    g_stutter.old_pos = 0.0f;
    g_stutter.old_start = 0.0f;
    g_stutter.old_loop_samples = 1200.0f;
    g_stutter.xfade_pos = 0.0f;
}

static void fxmm_reset_slot_state(fx_master_macro_slot_state_t *slot)
{
    if (slot == NULL)
    {
        return;
    }

    slot->wet = 0.0f;
    slot->ring_phase = 0.0f;
    slot->chop_phase = 0.0f;
    slot->pump_phase = 0.0f;
    slot->chop_gain = 1.0f;
    slot->pump_gain = 1.0f;
    slot->delay_samples = 2400.0f;
    slot->delay_feedback_lp = 0.0f;
    slot->wobble_phase = 0.0f;
    slot->freeze_gate = 0.0f;
    slot->stutter_pos = 0.0f;
    slot->stutter_start = 0.0f;
    slot->stutter_loop_samples = 1200.0f;
    slot->stutter_active = 0U;
    slot->color_smooth_l = 0.0f;
    slot->color_smooth_r = 0.0f;
    slot->color_amount = 0.0f;
    slot->color_coeff = 0.0f;
    slot->crush_hold_l = 0.0f;
    slot->crush_hold_r = 0.0f;
    slot->color_init = 0U;
    slot->crush_count = 0U;
    slot->delay_write = 0U;
    slot->delay_filled = 0U;
    slot->dc_x_l = 0.0f;
    slot->dc_y_l = 0.0f;
    slot->dc_x_r = 0.0f;
    slot->dc_y_r = 0.0f;
}

static float fxmm_phase_sine(float phase)
{
    float x = phase;
    while (x >= 1.0f)
    {
        x -= 1.0f;
    }
    if (x < 0.5f)
    {
        x = (4.0f * x) - 1.0f;
    }
    else
    {
        x = 3.0f - (4.0f * x);
    }
    return x * (1.57079632679f - (0.57079632679f * fabsf(x)));
}

static float fxmm_phase_triangle(float phase)
{
    float x = phase;
    while (x >= 1.0f)
    {
        x -= 1.0f;
    }
    return (x < 0.5f) ? ((4.0f * x) - 1.0f) : (3.0f - (4.0f * x));
}

static float fxmm_rate_hz_from_macro(float macro, float bpm_milli)
{
    static const float divs[] = { 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f };
    const uint32_t idx = (uint32_t)(fxmm_norm(macro) * 9.0f + 0.5f);
    const float bpm = (bpm_milli > 0.0f) ? (bpm_milli * 0.001f) : 120.0f;
    return (bpm * (1.0f / 60.0f)) * divs[(idx < 10U) ? idx : 9U];
}

static float fxmm_time_samples_from_macro(float macro, float bpm_milli, float min_s, float max_s)
{
    static const float beats[] = { 0.125f, 0.25f, 0.3333333f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    const uint32_t idx = (uint32_t)(fxmm_norm(macro) * 7.0f + 0.5f);
    const float bpm = (bpm_milli > 0.0f) ? (bpm_milli * 0.001f) : 120.0f;
    float seconds = (60.0f / bpm) * beats[(idx < 8U) ? idx : 7U];
    seconds = fxmm_clampf(seconds, min_s, max_s);
    return fxmm_clampf(seconds * g_sample_rate, 2.0f, FX_MASTER_MACRO_DELAY_MAX_F);
}

static float fxmm_delay_read(const fx_master_macro_slot_state_t *slot, uint8_t slot_index, float delay_samples)
{
    if ((slot == NULL) || (slot_index >= FX_MASTER_MACRO_SLOT_COUNT))
    {
        return 0.0f;
    }

    delay_samples = fxmm_clampf(delay_samples, 1.0f, FX_MASTER_MACRO_DELAY_MAX_F);
    const uint32_t needed = (uint32_t)delay_samples + 2U;
    if (slot->delay_filled < needed)
    {
        return 0.0f;
    }

    float pos = (float)slot->delay_write - delay_samples;
    while (pos < 0.0f)
    {
        pos += (float)FX_MASTER_MACRO_DELAY_MAX_SAMPLES;
    }
    while (pos >= (float)FX_MASTER_MACRO_DELAY_MAX_SAMPLES)
    {
        pos -= (float)FX_MASTER_MACRO_DELAY_MAX_SAMPLES;
    }

    const uint32_t i0 = (uint32_t)pos;
    const uint32_t i1 = (i0 + 1U) % FX_MASTER_MACRO_DELAY_MAX_SAMPLES;
    const float frac = pos - (float)i0;
    const float y0 = g_delay[slot_index][i0];
    const float y1 = g_delay[slot_index][i1];
    return y0 + ((y1 - y0) * frac);
}

static void fxmm_delay_write(fx_master_macro_slot_state_t *slot, uint8_t slot_index, float value)
{
    if ((slot == NULL) || (slot_index >= FX_MASTER_MACRO_SLOT_COUNT))
    {
        return;
    }

    g_delay[slot_index][slot->delay_write] = fxmm_clampf(value, -1.15f, 1.15f);
    slot->delay_write++;
    if (slot->delay_write >= FX_MASTER_MACRO_DELAY_MAX_SAMPLES)
    {
        slot->delay_write = 0U;
    }
    if (slot->delay_filled < FX_MASTER_MACRO_DELAY_MAX_SAMPLES)
    {
        slot->delay_filled++;
    }
}

static float fxmm_comb_delay_samples(float tune)
{
    const float freq = 90.0f * powf(2.0f, fxmm_norm(tune) * 5.5f);
    return fxmm_clampf(g_sample_rate / freq, 12.0f, 560.0f);
}

static float fxmm_stutter_size_samples(float size_norm, float bpm_milli)
{
    static const float beats[] = { 0.03125f, 0.0625f, 0.125f, 0.1666667f, 0.25f, 0.3333333f, 0.5f, 0.75f };
    const uint32_t idx = (uint32_t)(size_norm * 7.0f + 0.5f);
    const float bpm = (bpm_milli > 0.0f) ? (bpm_milli * 0.001f) : 120.0f;
    float seconds = (60.0f / bpm) * beats[(idx < 8U) ? idx : 7U];
    seconds = fxmm_clampf(seconds, 0.010f, 0.500f);
    return fxmm_clampf(seconds * g_sample_rate, 32.0f, FX_MASTER_MACRO_STUTTER_HISTORY_F);
}

static float fxmm_stutter_rate_mul(float rate_norm)
{
    static const float rates[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
    const uint32_t idx = (uint32_t)(rate_norm * 7.0f + 0.5f);
    return rates[(idx < 8U) ? idx : 7U];
}

static void fxmm_stutter_history_write(float left, float right)
{
    g_stutter_history_l[g_stutter.write] = fxmm_clampf(left, -1.15f, 1.15f);
    g_stutter_history_r[g_stutter.write] = fxmm_clampf(right, -1.15f, 1.15f);
    g_stutter.write++;
    if (g_stutter.write >= FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES)
    {
        g_stutter.write = 0U;
    }
    if (g_stutter.filled < FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES)
    {
        g_stutter.filled++;
    }
}

static void fxmm_stutter_stop_playback(void)
{
    g_stutter.playing = 0U;
    g_stutter.relatch_pending = 0U;
    g_stutter.xfade_active = 0U;
}

static void fxmm_stutter_update_pending_loop(float loop_samples)
{
    loop_samples = fxmm_clampf(loop_samples, 32.0f, FX_MASTER_MACRO_STUTTER_HISTORY_F);
    if (fabsf(loop_samples - g_stutter.pending_loop_samples) > 0.5f)
    {
        g_stutter.pending_loop_samples = loop_samples;
        if (g_stutter.playing != 0U)
        {
            g_stutter.relatch_pending = 1U;
        }
    }
}

static void fxmm_stutter_read_abs(float pos, float *out_l, float *out_r)
{
    while (pos < 0.0f)
    {
        pos += (float)FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES;
    }
    while (pos >= (float)FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES)
    {
        pos -= (float)FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES;
    }

    const uint32_t i0 = (uint32_t)pos;
    const float frac = pos - (float)i0;
    float l = g_stutter_history_l[i0];
    float r = g_stutter_history_r[i0];
    if (frac != 0.0f)
    {
        const uint32_t i1 = (i0 + 1U) % FX_MASTER_MACRO_STUTTER_HISTORY_SAMPLES;
        const float l1 = g_stutter_history_l[i1];
        const float r1 = g_stutter_history_r[i1];
        l += (l1 - l) * frac;
        r += (r1 - r) * frac;
    }
    if (out_l != NULL)
    {
        *out_l = l;
    }
    if (out_r != NULL)
    {
        *out_r = r;
    }
}

static float fxmm_loop_xfade_gain(float pos, float len)
{
    const float fade = fxmm_clampf(g_sample_rate * 0.0025f, 8.0f, 128.0f);
    if (len <= (fade * 2.0f))
    {
        return 1.0f;
    }
    if (pos < fade)
    {
        return pos / fade;
    }
    if (pos > (len - fade))
    {
        return (len - pos) / fade;
    }
    return 1.0f;
}

static float fxmm_stutter_recent_pos(float len)
{
    const float fade = fxmm_clampf(g_sample_rate * 0.0025f, 8.0f, 128.0f);
    if (len <= (fade + 1.0f))
    {
        return 0.0f;
    }
    return len - fade;
}

static void fxmm_stutter_latch_from_history(float len)
{
    len = fxmm_clampf(len, 32.0f, FX_MASTER_MACRO_STUTTER_HISTORY_F);
    if (g_stutter.filled < ((uint32_t)len + 2U))
    {
        return;
    }

    g_stutter.playing = 1U;
    g_stutter.relatch_pending = 0U;
    g_stutter.xfade_active = 0U;
    g_stutter.loop_samples = len;
    g_stutter.pending_loop_samples = len;
    g_stutter.start = (float)g_stutter.write - len;
    g_stutter.pos = fxmm_stutter_recent_pos(len);
}

static void fxmm_stutter_begin_relatch(float len)
{
    len = fxmm_clampf(len, 32.0f, FX_MASTER_MACRO_STUTTER_HISTORY_F);
    if (g_stutter.filled < ((uint32_t)len + 2U))
    {
        return;
    }

    g_stutter.old_pos = g_stutter.pos;
    g_stutter.old_start = g_stutter.start;
    g_stutter.old_loop_samples = g_stutter.loop_samples;
    g_stutter.start = (float)g_stutter.write - len;
    g_stutter.loop_samples = len;
    g_stutter.pending_loop_samples = len;
    g_stutter.pos = fxmm_stutter_recent_pos(len);
    g_stutter.xfade_pos = 0.0f;
    g_stutter.xfade_active = 1U;
    g_stutter.relatch_pending = 0U;
}

static void fxmm_stutter_process_sample(float dry_l,
                                        float dry_r,
                                        float rate,
                                        float *wet_l,
                                        float *wet_r)
{
    if ((wet_l == NULL) || (wet_r == NULL))
    {
        return;
    }

    if (g_stutter.playing == 0U)
    {
        fxmm_stutter_latch_from_history(g_stutter.pending_loop_samples);
    }

    if (g_stutter.playing == 0U)
    {
        fxmm_stutter_history_write(dry_l, dry_r);
        *wet_l = dry_l;
        *wet_r = dry_r;
        return;
    }

    if (g_stutter.relatch_pending != 0U)
    {
        fxmm_stutter_begin_relatch(g_stutter.pending_loop_samples);
    }

    float new_l = dry_l;
    float new_r = dry_r;
    fxmm_stutter_read_abs(g_stutter.start + g_stutter.pos, &new_l, &new_r);
    const float new_gain = fxmm_loop_xfade_gain(g_stutter.pos, g_stutter.loop_samples);
    new_l *= new_gain;
    new_r *= new_gain;

    if (g_stutter.xfade_active != 0U)
    {
        float old_l = dry_l;
        float old_r = dry_r;
        fxmm_stutter_read_abs(g_stutter.old_start + g_stutter.old_pos, &old_l, &old_r);
        const float old_gain = fxmm_loop_xfade_gain(g_stutter.old_pos, g_stutter.old_loop_samples);
        old_l *= old_gain;
        old_r *= old_gain;

        const float fade_samples = fxmm_clampf(g_sample_rate * 0.0025f, 8.0f, 128.0f);
        const float mix = fxmm_clampf(g_stutter.xfade_pos / fade_samples, 0.0f, 1.0f);
        *wet_l = old_l + ((new_l - old_l) * mix);
        *wet_r = old_r + ((new_r - old_r) * mix);

        g_stutter.old_pos += rate;
        while (g_stutter.old_pos >= g_stutter.old_loop_samples)
        {
            g_stutter.old_pos -= g_stutter.old_loop_samples;
        }
        g_stutter.xfade_pos += 1.0f;
        if (g_stutter.xfade_pos >= fade_samples)
        {
            g_stutter.xfade_active = 0U;
        }
    }
    else
    {
        *wet_l = new_l;
        *wet_r = new_r;
    }

    g_stutter.pos += rate;
    if (g_stutter.pos >= g_stutter.loop_samples)
    {
        while (g_stutter.pos >= g_stutter.loop_samples)
        {
            g_stutter.pos -= g_stutter.loop_samples;
        }
    }
}

static float fxmm_color_focus_hz(float focus_norm)
{
    const float shaped = focus_norm * focus_norm;
    return 2500.0f + (shaped * 11500.0f);
}

static float fxmm_color_amount_from_macro(float macro)
{
    const uint8_t raw = fxmm_u7(macro);
    if (raw == 64U)
    {
        return 0.0f;
    }
    if (raw < 64U)
    {
        return -(((float)(64U - raw) * (1.0f / 64.0f)) * 0.85f);
    }
    return ((float)(raw - 64U) * (1.0f / 63.0f)) * 0.85f;
}

static float fxmm_color_coeff_from_hz(float hz)
{
    const float omega = fxmm_clampf(hz / g_sample_rate, 0.0001f, 0.45f);
    return omega / (1.0f + omega);
}

static void fxmm_process_color_sample(fx_master_macro_slot_state_t *slot,
                                      float dry_l,
                                      float dry_r,
                                      float target_amount,
                                      float target_coeff,
                                      float *out_l,
                                      float *out_r)
{
    if ((slot == NULL) || (out_l == NULL) || (out_r == NULL))
    {
        return;
    }

    if (slot->color_init == 0U)
    {
        slot->color_smooth_l = dry_l;
        slot->color_smooth_r = dry_r;
        slot->color_amount = target_amount;
        slot->color_coeff = target_coeff;
        slot->color_init = 1U;
    }

    slot->color_amount = fxmm_smooth(slot->color_amount, target_amount, 0.0030f);
    slot->color_coeff = fxmm_smooth(slot->color_coeff, target_coeff, 0.0020f);
    slot->color_smooth_l += slot->color_coeff * (dry_l - slot->color_smooth_l);
    slot->color_smooth_r += slot->color_coeff * (dry_r - slot->color_smooth_r);

    *out_l = fxmm_clampf(dry_l + (slot->color_amount * (dry_l - slot->color_smooth_l)), -1.15f, 1.15f);
    *out_r = fxmm_clampf(dry_r + (slot->color_amount * (dry_r - slot->color_smooth_r)), -1.15f, 1.15f);
}

static uint32_t fxmm_get_bpm_milli(void)
{
    uint32_t bpm_milli = 120000U;
    if ((seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL)
            && (seq_runtime_is_external_tempo_valid() != 0U))
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
    }
    else
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
    }

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

static uint8_t fxmm_find_macro_fx_track(uint8_t *out_track)
{
    uint8_t track = 0U;
    if (track_topology_find_special(TRACK_TOPOLOGY_ROLE_FX, 0U, &track) == 0U)
    {
        return 0U;
    }

    const track_runtime_ctx_t *ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SPECIAL_FX)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SPECIAL_FX))
    {
        return 0U;
    }
    if (out_track != NULL)
    {
        *out_track = track;
    }
    return 1U;
}

static float fxmm_drive_sat(float x)
{
    if (x > 1.0f)
    {
        return 1.0f;
    }
    if (x < -1.0f)
    {
        return -1.0f;
    }
    const float x2 = x * x;
    return x * (1.5f - (0.5f * x2));
}

static float fxmm_drive_clip(float x, float clip_gain)
{
    x *= clip_gain;
    if (x > 1.08f)
    {
        x = 1.08f;
    }
    else if (x < -1.08f)
    {
        x = -1.08f;
    }
    return x;
}

static float fxmm_drive_limit(float x)
{
    if (x > 0.82f)
    {
        x = 0.82f + ((x - 0.82f) * 0.28f);
        return (x > 1.03f) ? 1.03f : x;
    }
    if (x < -0.82f)
    {
        x = -0.82f + ((x + 0.82f) * 0.28f);
        return (x < -1.03f) ? -1.03f : x;
    }
    return x;
}

static void fxmm_process_drive_stereo(float dry_l,
                                      float dry_r,
                                      float drive,
                                      float bias,
                                      float bias_sat,
                                      float comp,
                                      float out_gain,
                                      float pre_coeff,
                                      float pre_amt,
                                      float tone_coeff,
                                      float tone_amt,
                                      float clip_gain,
                                      uint8_t fast,
                                      float *pre_l,
                                      float *pre_r,
                                      float *tone_l,
                                      float *tone_r,
                                      float *out_l,
                                      float *out_r)
{
    *pre_l += pre_coeff * (dry_l - *pre_l);
    *pre_r += pre_coeff * (dry_r - *pre_r);

    float in_l = dry_l;
    float in_r = dry_r;
    if (fast == 0U)
    {
        in_l += (dry_l - *pre_l) * pre_amt;
        in_r += (dry_r - *pre_r) * pre_amt;
    }

    float driven_l = (fxmm_drive_sat((in_l * drive) + bias) - bias_sat) * comp;
    float driven_r = (fxmm_drive_sat((in_r * drive) + bias) - bias_sat) * comp;

    if (fast == 0U)
    {
        driven_l = fxmm_drive_clip(driven_l, clip_gain);
        driven_r = fxmm_drive_clip(driven_r, clip_gain);
    }

    *tone_l += tone_coeff * (driven_l - *tone_l);
    *tone_r += tone_coeff * (driven_r - *tone_r);
    driven_l += (driven_l - *tone_l) * tone_amt;
    driven_r += (driven_r - *tone_r) * tone_amt;
    driven_l = fxmm_drive_limit(driven_l * out_gain);
    driven_r = fxmm_drive_limit(driven_r * out_gain);

    *out_l = fxmm_clampf(driven_l, -1.15f, 1.15f);
    *out_r = fxmm_clampf(driven_r, -1.15f, 1.15f);
}

static float fxmm_process_crush_sample(float in, float bits_norm)
{
    const uint32_t bits = 16U - (uint32_t)(bits_norm * 12.0f + 0.5f);
    const float steps = (float)(1UL << ((bits < 4U) ? 4U : bits));
    return floorf((in * steps) + 0.5f) / steps;
}

static float fxmm_ring_modulator(fx_master_macro_slot_state_t *slot, float freq_norm, float color)
{
    const float freq = 0.5f + (freq_norm * freq_norm * 1499.5f);
    slot->ring_phase += freq / g_sample_rate;
    while (slot->ring_phase >= 1.0f)
    {
        slot->ring_phase -= 1.0f;
    }

    const float sine = fxmm_phase_sine(slot->ring_phase);
    const float tri = fxmm_phase_triangle(slot->ring_phase);
    const float square = (slot->ring_phase < 0.5f) ? 1.0f : -1.0f;
    const float dirt = fxmm_clampf(square + (sine * 0.35f) + (tri * 0.15f), -1.0f, 1.0f);
    const uint32_t color_idx = (uint32_t)(color * 3.0f + 0.5f);
    switch ((color_idx < 4U) ? color_idx : 3U)
    {
        case 1U:
            return tri;
        case 2U:
            return square;
        case 3U:
            return dirt;
        default:
            return sine;
    }
}

static float fxmm_dc_block(float in, float *x1, float *y1)
{
    const float y = in - *x1 + (FX_MASTER_MACRO_DC_ALPHA * *y1);
    *x1 = in;
    *y1 = y;
    return y;
}

static void fxmm_process_slot(fx_master_macro_slot_state_t *slot,
                              uint8_t slot_index,
                              uint8_t type,
                              float level_raw,
                              float macro_a,
                              float macro_b,
                              float *left,
                              float *right,
                              uint32_t frames,
                              float bpm_milli,
                              uint8_t stutter_owner_slot,
                              uint8_t freeze_owner_slot)
{
    const uint8_t active_type = fxmm_type_is_active(type);
    const float target_wet = ((active_type == 0U) || (level_raw <= 0.0f))
            ? 0.0f
            : fxmm_norm(level_raw);
    const float a = fxmm_norm(macro_a);
    const float b = fxmm_norm(macro_b);

    if (slot->last_type != type)
    {
        slot->last_type = type;
        fxmm_reset_slot_state(slot);
    }

    if (active_type == 0U)
    {
        slot->wet = 0.0f;
        return;
    }

    if ((type >= FX_MASTER_MACRO_CHORUS_MICRO)
            && (type <= FX_MASTER_MACRO_CHORUS_JUNO))
    {
        if (level_raw <= 0.0f)
        {
            slot->wet = 0.0f;
            return;
        }
        fx_chorus_bench_process(left, right, frames,
                                (fx_chorus_bench_model_t)(type - FX_MASTER_MACRO_CHORUS_MICRO + 1U),
                                target_wet, a, b);
        slot->wet = target_wet;
        return;
    }

    if ((fxmm_type_is_unique_history(type) != 0U) && (slot_index != stutter_owner_slot))
    {
        slot->wet = 0.0f;
        return;
    }
    if ((fxmm_type_is_unique_freeze(type) != 0U) && (slot_index != freeze_owner_slot))
    {
        slot->wet = 0.0f;
        return;
    }

    if (type == FX_MASTER_MACRO_STUTTER)
    {
        const float stutter_loop = fxmm_stutter_size_samples(a, bpm_milli);
        const float stutter_rate = fxmm_stutter_rate_mul(b);
        fxmm_stutter_update_pending_loop(stutter_loop);
        slot->wet = (level_raw > 0.0f) ? 1.0f : 0.0f;

        if (level_raw <= 0.0f)
        {
            fxmm_stutter_stop_playback();
            for (uint32_t i = 0U; i < frames; ++i)
            {
                fxmm_stutter_history_write(left[i], right[i]);
            }
            return;
        }

        for (uint32_t i = 0U; i < frames; ++i)
        {
            float wet_l = left[i];
            float wet_r = right[i];
            fxmm_stutter_process_sample(left[i], right[i], stutter_rate, &wet_l, &wet_r);
            left[i] = fxmm_audio_clampf(wet_l, -1.20f, 1.20f);
            right[i] = fxmm_audio_clampf(wet_r, -1.20f, 1.20f);
        }
        return;
    }

    const uint8_t keeps_history = (((type == FX_MASTER_MACRO_STUTTER) && (slot_index == stutter_owner_slot))
            || ((type == FX_MASTER_MACRO_FREEZE) && (slot_index == freeze_owner_slot))) ? 1U : 0U;
    if ((target_wet <= 0.0f) && (slot->wet <= 0.000001f) && (keeps_history == 0U))
    {
        slot->wet = 0.0f;
        return;
    }

    float freeze_target_delay = 0.0f;
    float comb_target_delay = 0.0f;
    float wobble_rate = 0.0f;
    float wobble_depth_samples = 0.0f;
    float color_amount = 0.0f;
    float color_coeff = 0.0f;
    float drive_gain = 1.0f;
    float drive_bias = 0.0f;
    float drive_bias_sat = 0.0f;
    float drive_comp = 1.0f;
    float drive_out_gain = 1.0f;
    float drive_pre_coeff = 0.0f;
    float drive_pre_amt = 0.0f;
    float drive_tone_coeff = 0.0f;
    float drive_tone_amt = 0.0f;
    float drive_clip_gain = 1.0f;
    uint8_t drive_fast = 0U;
    if (type == FX_MASTER_MACRO_FREEZE)
    {
        freeze_target_delay = fxmm_time_samples_from_macro(macro_a, bpm_milli, 0.060f, 0.800f);
        slot->delay_samples = fxmm_smooth(slot->delay_samples, freeze_target_delay, 0.0012f);
        const float freeze_target_wet = target_wet;
        const float freeze_target_gate = (level_raw > 0.0f) ? 1.0f : 0.0f;
        const uint8_t hold_step = (uint8_t)(((uint32_t)fxmm_u7(macro_b) * 3U + 63U) / 127U);
        static const float k_freeze_feedback[] = { 0.68f, 0.88f, 0.965f, 0.9995f };
        static const float k_freeze_return_gain[] = { 1.35f, 1.35f, 1.42f, 1.48f };
        const float hold_fb = k_freeze_feedback[(hold_step < 4U) ? hold_step : 3U];
        const float return_gain = k_freeze_return_gain[(hold_step < 4U) ? hold_step : 3U];
        const uint32_t needed = (uint32_t)fxmm_clampf(slot->delay_samples, 1.0f, FX_MASTER_MACRO_DELAY_MAX_F) + 2U;
        for (uint32_t i = 0U; i < frames; ++i)
        {
            slot->wet = fxmm_smooth(slot->wet, freeze_target_wet, FX_MASTER_MACRO_SMOOTH_FAST);
            slot->freeze_gate = fxmm_smooth(slot->freeze_gate, freeze_target_gate, 0.015f);

            const float dry_l = left[i];
            const float dry_r = right[i];
            if (slot->delay_filled < needed)
            {
                fxmm_delay_write(slot, slot_index, (dry_l + dry_r) * 0.5f);
                continue;
            }

            const float delayed = fxmm_delay_read(slot, slot_index, slot->delay_samples);
            const float input = (dry_l + dry_r) * 0.5f;
            slot->delay_feedback_lp += (delayed - slot->delay_feedback_lp) * 0.12f;
            const float input_send = 1.0f - (slot->freeze_gate * (0.20f + (0.80f * slot->wet)));
            const float dry_gain = 1.0f - (slot->wet * slot->wet);
            const float repeat_gain = slot->wet * return_gain;
            const float write = (input * input_send)
                    + (slot->delay_feedback_lp * hold_fb * slot->freeze_gate);
            fxmm_delay_write(slot, slot_index, write);
            const float wet_return = delayed * repeat_gain;
            left[i] = fxmm_audio_clampf((dry_l * dry_gain) + wet_return, -1.20f, 1.20f);
            right[i] = fxmm_audio_clampf((dry_r * dry_gain) + wet_return, -1.20f, 1.20f);
        }
        return;
    }
    else if (type == FX_MASTER_MACRO_COMB)
    {
        comb_target_delay = fxmm_comb_delay_samples(macro_a);
    }
    else if (type == FX_MASTER_MACRO_WOBBLE)
    {
        wobble_rate = 0.06f + (a * a * 7.5f);
        wobble_depth_samples = (0.0008f + (b * 0.0105f)) * g_sample_rate;
    }
    else if (type == FX_MASTER_MACRO_COLOR)
    {
        color_amount = fxmm_color_amount_from_macro(macro_a);
        color_coeff = fxmm_color_coeff_from_hz(fxmm_color_focus_hz(b));
    }
    else if (type == FX_MASTER_MACRO_DRIVE)
    {
        drive_fast = (a < 0.58f) ? 1U : 0U;
        drive_gain = 1.0f + (a * a * 42.0f);
        drive_bias = (0.15f * a) + (0.12f * a * a);
        drive_bias_sat = fxmm_drive_sat(drive_bias);
        drive_comp = 1.0f / (1.0f + (a * 0.06f) + (a * a * 0.06f));
        drive_out_gain = 1.0f / (1.0f + (a * 0.18f) + (a * a * 2.60f));
        drive_pre_coeff = 0.016f + (b * 0.110f);
        drive_pre_amt = (drive_fast != 0U) ? 0.0f : (a * (-0.25f + (b * 1.85f)));
        drive_tone_coeff = 0.040f + (b * 0.230f);
        drive_tone_amt = (b - 0.45f) * 1.70f;
        drive_clip_gain = 1.0f + (a * a * 0.22f);
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        slot->wet = fxmm_smooth(slot->wet, target_wet, FX_MASTER_MACRO_SMOOTH_FAST);
        const float dry_l = left[i];
        const float dry_r = right[i];
        float wet_l = dry_l;
        float wet_r = dry_r;

        switch (type)
        {
            case FX_MASTER_MACRO_DRIVE:
                if (slot->color_init == 0U)
                {
                    slot->color_smooth_l = dry_l;
                    slot->color_smooth_r = dry_r;
                    slot->dc_x_l = dry_l;
                    slot->dc_x_r = dry_r;
                    slot->color_init = 1U;
                }
                fxmm_process_drive_stereo(dry_l,
                                          dry_r,
                                          drive_gain,
                                          drive_bias,
                                          drive_bias_sat,
                                          drive_comp,
                                          drive_out_gain,
                                          drive_pre_coeff,
                                          drive_pre_amt,
                                          drive_tone_coeff,
                                          drive_tone_amt,
                                          drive_clip_gain,
                                          drive_fast,
                                          &slot->dc_x_l,
                                          &slot->dc_x_r,
                                          &slot->color_smooth_l,
                                          &slot->color_smooth_r,
                                          &wet_l,
                                          &wet_r);
                break;

            case FX_MASTER_MACRO_CRUSH:
            {
                const uint16_t hold = 1U + (uint16_t)(b * b * 95.0f);
                if (slot->crush_count == 0U)
                {
                    slot->crush_hold_l = fxmm_process_crush_sample(dry_l, a);
                    slot->crush_hold_r = fxmm_process_crush_sample(dry_r, a);
                    slot->crush_count = hold;
                }
                slot->crush_count--;
                wet_l = slot->crush_hold_l;
                wet_r = slot->crush_hold_r;
                break;
            }

            case FX_MASTER_MACRO_WOBBLE:
            {
                slot->wobble_phase += wobble_rate / g_sample_rate;
                while (slot->wobble_phase >= 1.0f)
                {
                    slot->wobble_phase -= 1.0f;
                }
                const float mod = fxmm_phase_sine(slot->wobble_phase);
                const float target_delay = (0.009f * g_sample_rate) + (mod * wobble_depth_samples);
                const float delay = fxmm_clampf(target_delay, 2.0f, 0.024f * g_sample_rate);
                const float delayed = fxmm_delay_read(slot, slot_index, delay);
                fxmm_delay_write(slot, slot_index, (dry_l + dry_r) * 0.5f);
                wet_l = delayed;
                wet_r = delayed;
                break;
            }

            case FX_MASTER_MACRO_COMB:
            {
                slot->delay_samples = fxmm_smooth(slot->delay_samples, comb_target_delay, 0.0025f);
                const float delayed = fxmm_delay_read(slot, slot_index, slot->delay_samples);
                slot->delay_feedback_lp += (delayed - slot->delay_feedback_lp) * 0.28f;
                const float fb = fxmm_clampf(b * 0.80f, 0.0f, 0.80f);
                const float input = (dry_l + dry_r) * 0.5f;
                fxmm_delay_write(slot, slot_index, input + (slot->delay_feedback_lp * fb));
                wet_l = fxmm_clampf(dry_l + (delayed * 0.72f), -1.15f, 1.15f);
                wet_r = fxmm_clampf(dry_r + (delayed * 0.72f), -1.15f, 1.15f);
                break;
            }

            case FX_MASTER_MACRO_RING:
            {
                const float mod = fxmm_ring_modulator(slot, a, b);
                wet_l = fxmm_dc_block(dry_l * mod, &slot->dc_x_l, &slot->dc_y_l) * 0.85f;
                wet_r = fxmm_dc_block(dry_r * mod, &slot->dc_x_r, &slot->dc_y_r) * 0.85f;
                break;
            }

            case FX_MASTER_MACRO_CHOP:
            {
                const float rate = fxmm_rate_hz_from_macro(macro_a, bpm_milli);
                slot->chop_phase += rate / g_sample_rate;
                while (slot->chop_phase >= 1.0f)
                {
                    slot->chop_phase -= 1.0f;
                }
                const float tri = (slot->chop_phase < 0.5f) ? (slot->chop_phase * 2.0f) : ((1.0f - slot->chop_phase) * 2.0f);
                const float square = (slot->chop_phase < 0.5f) ? 1.0f : 0.0f;
                const float shape_gain = tri + ((square - tri) * b);
                const float min_gain = 1.0f - (slot->wet * 0.98f);
                const float target = min_gain + ((1.0f - min_gain) * shape_gain);
                slot->chop_gain = fxmm_smooth(slot->chop_gain, target, FX_MASTER_MACRO_SMOOTH_GAIN);
                wet_l = dry_l * slot->chop_gain;
                wet_r = dry_r * slot->chop_gain;
                break;
            }

            case FX_MASTER_MACRO_PUMP:
            {
                const float rate = fxmm_rate_hz_from_macro(macro_a, bpm_milli);
                slot->pump_phase += rate / g_sample_rate;
                while (slot->pump_phase >= 1.0f)
                {
                    slot->pump_phase -= 1.0f;
                }
                const float min_gain = 1.0f - (slot->wet * 0.82f);
                const float rel = 0.10f + (b * 0.85f);
                float env = slot->pump_phase / rel;
                if (env > 1.0f)
                {
                    env = 1.0f;
                }
                env = env * env;
                const float target = min_gain + ((1.0f - min_gain) * env);
                slot->pump_gain = fxmm_smooth(slot->pump_gain, target, FX_MASTER_MACRO_SMOOTH_GAIN);
                wet_l = dry_l * slot->pump_gain;
                wet_r = dry_r * slot->pump_gain;
                break;
            }

            case FX_MASTER_MACRO_COLOR:
                fxmm_process_color_sample(slot,
                                          dry_l,
                                          dry_r,
                                          color_amount,
                                          color_coeff,
                                          &wet_l,
                                          &wet_r);
                break;

            default:
                wet_l = dry_l;
                wet_r = dry_r;
                break;
        }

        left[i] = (dry_l * (1.0f - slot->wet)) + (wet_l * slot->wet);
        right[i] = (dry_r * (1.0f - slot->wet)) + (wet_r * slot->wet);
        if (slot->wet > 0.000001f)
        {
            left[i] = fxmm_audio_clampf(left[i], -1.20f, 1.20f);
            right[i] = fxmm_audio_clampf(right[i], -1.20f, 1.20f);
        }
    }
}

void fx_master_macro_init(float sample_rate)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_stutter_history_l, 0, sizeof(g_stutter_history_l));
    memset(g_stutter_history_r, 0, sizeof(g_stutter_history_r));
    fxmm_reset_stutter_state();
    g_sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT;
    g_fxmm_muted = 0U;
    g_fxmm_mute_gain = 1.0f;
}

void fx_master_macro_set_mute(uint8_t muted)
{
    g_fxmm_muted = (muted != 0U) ? 1U : 0U;
}

void fx_master_macro_process_block(float *left, float *right, uint32_t frames)
{
    g_fxmm_diag_enabled = audio_track_diag_is_enabled();
    uint8_t track = 0U;
    if ((left == NULL) || (right == NULL) || (frames == 0U) || (fxmm_find_macro_fx_track(&track) == 0U))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    memcpy(g_fxmm_dry_l, left, frames * sizeof(float));
    memcpy(g_fxmm_dry_r, right, frames * sizeof(float));

    const track_tone_sound_state_t *state = track_tone_sound_state_get_const(track);
    if (state == NULL)
    {
        return;
    }

    const float bpm_milli = (float)fxmm_get_bpm_milli();
    uint8_t stutter_owner_slot = FX_MASTER_MACRO_STUTTER_OWNER_NONE;
    uint8_t freeze_owner_slot = FX_MASTER_MACRO_STUTTER_OWNER_NONE;
    uint8_t chorus_owner_slot = FX_MASTER_MACRO_STUTTER_OWNER_NONE;
    for (uint8_t slot = 0U; slot < FX_MASTER_MACRO_SLOT_COUNT; ++slot)
    {
        const uint8_t type = fxmm_u7(state->macro_fx.type[slot]);
        if ((type == FX_MASTER_MACRO_STUTTER) && (stutter_owner_slot == FX_MASTER_MACRO_STUTTER_OWNER_NONE))
        {
            stutter_owner_slot = slot;
        }
        if ((type == FX_MASTER_MACRO_FREEZE) && (freeze_owner_slot == FX_MASTER_MACRO_STUTTER_OWNER_NONE))
        {
            freeze_owner_slot = slot;
        }
        if ((type >= FX_MASTER_MACRO_CHORUS_MICRO)
                && (type <= FX_MASTER_MACRO_CHORUS_JUNO)
                && (chorus_owner_slot == FX_MASTER_MACRO_STUTTER_OWNER_NONE))
        {
            chorus_owner_slot = slot;
        }
    }
    if (g_stutter.owner_slot != stutter_owner_slot)
    {
        fxmm_reset_stutter_state();
        g_stutter.owner_slot = stutter_owner_slot;
    }

    for (uint8_t slot = 0U; slot < FX_MASTER_MACRO_SLOT_COUNT; ++slot)
    {
        const uint8_t type = fxmm_u7(state->macro_fx.type[slot]);
        if ((type >= FX_MASTER_MACRO_CHORUS_MICRO)
                && (type <= FX_MASTER_MACRO_CHORUS_JUNO)
                && (slot != chorus_owner_slot))
        {
            g_slots[slot].wet = 0.0f;
            continue;
        }
        fxmm_process_slot(&g_slots[slot],
                          slot,
                          type,
                          state->macro_fx.level[slot],
                          state->macro_fx.macro_a[slot],
                          state->macro_fx.macro_b[slot],
                          left,
                          right,
                          frames,
                          bpm_milli,
                          stutter_owner_slot,
                          freeze_owner_slot);
    }

    const float target = (g_fxmm_muted != 0U) ? 0.0f : 1.0f;
    const float fade_samples = g_sample_rate * FX_MASTER_MACRO_MUTE_FADE_SECONDS;
    const float step = (fade_samples > 1.0f) ? (1.0f / fade_samples) : 1.0f;
    for (uint32_t i = 0U; i < frames; ++i)
    {
        if (g_fxmm_mute_gain < target)
        {
            g_fxmm_mute_gain += step;
            if (g_fxmm_mute_gain > target) g_fxmm_mute_gain = target;
        }
        else if (g_fxmm_mute_gain > target)
        {
            g_fxmm_mute_gain -= step;
            if (g_fxmm_mute_gain < target) g_fxmm_mute_gain = target;
        }
        left[i] = g_fxmm_dry_l[i] + ((left[i] - g_fxmm_dry_l[i]) * g_fxmm_mute_gain);
        right[i] = g_fxmm_dry_r[i] + ((right[i] - g_fxmm_dry_r[i]) * g_fxmm_mute_gain);
    }
}

void fx_master_macro_get_diag_state(fx_master_macro_diag_state_t *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    uint8_t track = 0U;
    if (fxmm_find_macro_fx_track(&track) == 0U)
    {
        return;
    }
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    if (state == NULL)
    {
        return;
    }
    for (uint8_t slot = 0U; slot < FX_MASTER_MACRO_DIAG_SLOT_COUNT; ++slot)
    {
        out->type[slot] = fxmm_u7(state->macro_fx.type[slot]);
        out->level[slot] = fxmm_u7(state->macro_fx.level[slot]);
        if ((fxmm_type_is_active(out->type[slot]) != 0U) && (out->level[slot] != 0U))
        {
            out->active_mask |= (uint8_t)(1U << slot);
        }
    }
}
