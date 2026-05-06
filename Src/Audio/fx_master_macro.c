#include "fx_master_macro.h"

#include <math.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "audio_float.h"
#include "memory_layout.h"

#define FX_MASTER_MACRO_SLOT_COUNT 4U
#define FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT 48000.0f
#define FX_MASTER_MACRO_SMOOTH_FAST 0.08f
#define FX_MASTER_MACRO_SMOOTH_GAIN 0.12f
#define FX_MASTER_MACRO_DC_ALPHA 0.995f
#define FX_MASTER_MACRO_DELAY_MAX_SAMPLES 48000U
#define FX_MASTER_MACRO_DELAY_MAX_F ((float)(FX_MASTER_MACRO_DELAY_MAX_SAMPLES - 2U))
#define FX_MASTER_MACRO_TALK_FORMANT_COUNT 3U

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
    float pitch_phase_a;
    float pitch_phase_b;
    float talk_low_l[FX_MASTER_MACRO_TALK_FORMANT_COUNT];
    float talk_band_l[FX_MASTER_MACRO_TALK_FORMANT_COUNT];
    float talk_low_r[FX_MASTER_MACRO_TALK_FORMANT_COUNT];
    float talk_band_r[FX_MASTER_MACRO_TALK_FORMANT_COUNT];
    float crush_hold_l;
    float crush_hold_r;
    uint16_t crush_count;
    uint32_t delay_write;
    uint32_t delay_filled;
    float dc_x_l;
    float dc_y_l;
    float dc_x_r;
    float dc_y_r;
} fx_master_macro_slot_state_t;

AUDIO_HOT static fx_master_macro_slot_state_t g_slots[FX_MASTER_MACRO_SLOT_COUNT];
AUDIO_COLD_SDRAM static float g_delay[FX_MASTER_MACRO_SLOT_COUNT][FX_MASTER_MACRO_DELAY_MAX_SAMPLES];
static float g_sample_rate = FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT;

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
        case FX_MASTER_MACRO_ECHO:
        case FX_MASTER_MACRO_WOBBLE:
        case FX_MASTER_MACRO_COMB:
        case FX_MASTER_MACRO_RING:
        case FX_MASTER_MACRO_CHOP:
        case FX_MASTER_MACRO_PUMP:
        case FX_MASTER_MACRO_PITCH:
        case FX_MASTER_MACRO_TALK:
        case FX_MASTER_MACRO_STUTTER:
        case FX_MASTER_MACRO_FREEZE:
            return 1U;
        default:
            return 0U;
    }
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
    slot->pitch_phase_a = 0.0f;
    slot->pitch_phase_b = 0.5f;
    for (uint32_t i = 0U; i < FX_MASTER_MACRO_TALK_FORMANT_COUNT; ++i)
    {
        slot->talk_low_l[i] = 0.0f;
        slot->talk_band_l[i] = 0.0f;
        slot->talk_low_r[i] = 0.0f;
        slot->talk_band_r[i] = 0.0f;
    }
    slot->crush_hold_l = 0.0f;
    slot->crush_hold_r = 0.0f;
    slot->crush_count = 0U;
    slot->delay_write = 0U;
    slot->delay_filled = 0U;
    slot->dc_x_l = 0.0f;
    slot->dc_y_l = 0.0f;
    slot->dc_x_r = 0.0f;
    slot->dc_y_r = 0.0f;
}

static float fxmm_softclip(float x)
{
    return x / (1.0f + fabsf(x));
}

static float fxmm_fold(float x)
{
    if (x > 1.0f)
    {
        x = 2.0f - x;
    }
    else if (x < -1.0f)
    {
        x = -2.0f - x;
    }
    return fxmm_clampf(x, -1.0f, 1.0f);
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

static float fxmm_echo_feedback(float feedback_norm)
{
    return fxmm_clampf(feedback_norm * 0.74f, 0.0f, 0.74f);
}

static float fxmm_stutter_size_samples(float size_norm, float bpm_milli)
{
    static const float beats[] = { 0.03125f, 0.0625f, 0.125f, 0.1666667f, 0.25f, 0.3333333f, 0.5f, 0.75f };
    const uint32_t idx = (uint32_t)(size_norm * 7.0f + 0.5f);
    const float bpm = (bpm_milli > 0.0f) ? (bpm_milli * 0.001f) : 120.0f;
    float seconds = (60.0f / bpm) * beats[(idx < 8U) ? idx : 7U];
    seconds = fxmm_clampf(seconds, 0.010f, 0.500f);
    return fxmm_clampf(seconds * g_sample_rate, 32.0f, 24000.0f);
}

static float fxmm_stutter_rate_mul(float rate_norm)
{
    static const float rates[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
    const uint32_t idx = (uint32_t)(rate_norm * 7.0f + 0.5f);
    return rates[(idx < 8U) ? idx : 7U];
}

static float fxmm_delay_read_abs(uint8_t slot_index, float pos)
{
    if (slot_index >= FX_MASTER_MACRO_SLOT_COUNT)
    {
        return 0.0f;
    }
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

static float fxmm_talk_formant(float in, float freq, float damp, float *low, float *band)
{
    const float f = fxmm_clampf((2.0f * freq) / g_sample_rate, 0.001f, 0.42f);
    *low += f * *band;
    const float high = in - *low - (damp * *band);
    *band += f * high;
    *low = fxmm_clampf(*low, -2.0f, 2.0f);
    *band = fxmm_clampf(*band, -2.0f, 2.0f);
    return *band;
}

static float fxmm_talk_process(fx_master_macro_slot_state_t *slot, float in, uint8_t right, float vowel, float tone)
{
    static const float formants[5U][FX_MASTER_MACRO_TALK_FORMANT_COUNT] = {
        { 730.0f, 1090.0f, 2440.0f },
        { 530.0f, 1840.0f, 2480.0f },
        { 270.0f, 2290.0f, 3010.0f },
        { 570.0f, 840.0f, 2410.0f },
        { 300.0f, 870.0f, 2240.0f }
    };
    static const float gains[FX_MASTER_MACRO_TALK_FORMANT_COUNT] = { 0.58f, 0.34f, 0.20f };
    const float scaled = vowel * 4.0f;
    uint32_t idx = (uint32_t)scaled;
    if (idx >= 4U)
    {
        idx = 4U;
    }
    const uint32_t next = (idx < 4U) ? (idx + 1U) : idx;
    const float frac = scaled - (float)idx;
    const float shift = 0.72f + (tone * 0.62f);
    const float damp = 0.28f - (tone * 0.10f);
    float out = in * (0.12f + (0.18f * (1.0f - tone)));

    for (uint32_t i = 0U; i < FX_MASTER_MACRO_TALK_FORMANT_COUNT; ++i)
    {
        const float f0 = formants[idx][i];
        const float f1 = formants[next][i];
        const float freq = (f0 + ((f1 - f0) * frac)) * shift;
        float *low = (right != 0U) ? &slot->talk_low_r[i] : &slot->talk_low_l[i];
        float *band = (right != 0U) ? &slot->talk_band_r[i] : &slot->talk_band_l[i];
        out += fxmm_talk_formant(in, freq, damp, low, band) * gains[i];
    }

    return fxmm_clampf(out * (0.82f + (tone * 0.34f)), -1.15f, 1.15f);
}

static float fxmm_pitch_ratio(float semi_norm, float fine_norm)
{
    uint32_t semi_step = (uint32_t)(semi_norm * 24.0f + 0.5f);
    if (semi_step > 24U)
    {
        semi_step = 24U;
    }
    const float semis = (float)((int32_t)semi_step - 12);
    const float cents = ((fine_norm * 2.0f) - 1.0f) * 100.0f;
    return powf(2.0f, (semis + (cents * 0.01f)) * (1.0f / 12.0f));
}

static float fxmm_pitch_voice(const fx_master_macro_slot_state_t *slot, uint8_t slot_index, float phase, float ratio)
{
    const float window = fxmm_clampf(g_sample_rate * 0.045f, 512.0f, 4096.0f);
    const float base = window + 8.0f;
    const float sweep = (ratio >= 1.0f) ? (1.0f - phase) : phase;
    const float delay = base + (sweep * window);
    return fxmm_delay_read(slot, slot_index, delay);
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

static uint8_t fxmm_find_master_fx_track(uint8_t *out_track)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *ctx = track_runtime_get_ctx(track);
        if ((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX))
        {
            if (out_track != NULL)
            {
                *out_track = track;
            }
            return 1U;
        }
    }
    return 0U;
}

static float fxmm_process_drive(float in, float level, float tone, float shape)
{
    const float drive = 1.0f + (level * level * 5.0f);
    const uint32_t shape_idx = (uint32_t)(shape * 3.0f + 0.5f);
    float shaped = fxmm_softclip(in * drive);
    switch ((shape_idx < 4U) ? shape_idx : 3U)
    {
        case 1U:
            shaped = fxmm_clampf(in * drive * 0.75f, -1.0f, 1.0f);
            break;
        case 2U:
            shaped = fxmm_clampf(in * drive * 1.20f, -1.0f, 1.0f);
            break;
        case 3U:
            shaped = fxmm_fold(in * (1.0f + (level * 3.0f)));
            break;
        default:
            break;
    }
    const float comp = 1.0f / (1.0f + (level * 1.6f));
    const float dark = shaped * (0.62f + (0.38f * tone));
    return dark * comp;
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
                              float bpm_milli)
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

    const uint8_t keeps_history = ((type == FX_MASTER_MACRO_STUTTER) || (type == FX_MASTER_MACRO_PITCH)) ? 1U : 0U;
    if ((target_wet <= 0.0f) && (slot->wet <= 0.000001f) && (keeps_history == 0U))
    {
        slot->wet = 0.0f;
        return;
    }

    float echo_target_delay = 0.0f;
    float freeze_target_delay = 0.0f;
    float comb_target_delay = 0.0f;
    float wobble_rate = 0.0f;
    float wobble_depth_samples = 0.0f;
    float stutter_loop = 0.0f;
    float stutter_rate = 1.0f;
    float pitch_ratio = 1.0f;
    if (type == FX_MASTER_MACRO_ECHO)
    {
        echo_target_delay = fxmm_time_samples_from_macro(macro_a, bpm_milli, 0.040f, 0.950f);
    }
    else if (type == FX_MASTER_MACRO_FREEZE)
    {
        freeze_target_delay = fxmm_time_samples_from_macro(macro_a, bpm_milli, 0.060f, 0.800f);
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
    else if (type == FX_MASTER_MACRO_STUTTER)
    {
        stutter_loop = fxmm_stutter_size_samples(a, bpm_milli);
        stutter_rate = fxmm_stutter_rate_mul(b);
    }
    else if (type == FX_MASTER_MACRO_PITCH)
    {
        pitch_ratio = fxmm_pitch_ratio(a, b);
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        slot->wet = fxmm_smooth(slot->wet, target_wet, FX_MASTER_MACRO_SMOOTH_FAST);
        const float dry_l = left[i];
        const float dry_r = right[i];
        const float dry_m = (dry_l + dry_r) * 0.5f;
        float wet_l = dry_l;
        float wet_r = dry_r;

        switch (type)
        {
            case FX_MASTER_MACRO_DRIVE:
                wet_l = fxmm_process_drive(dry_l, slot->wet, a, b);
                wet_r = fxmm_process_drive(dry_r, slot->wet, a, b);
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

            case FX_MASTER_MACRO_ECHO:
            {
                slot->delay_samples = fxmm_smooth(slot->delay_samples, echo_target_delay, 0.0015f);
                const float delayed = fxmm_delay_read(slot, slot_index, slot->delay_samples);
                const float side = fxmm_delay_read(slot, slot_index, slot->delay_samples + 31.0f);
                slot->delay_feedback_lp += (delayed - slot->delay_feedback_lp) * 0.18f;
                const float fb = fxmm_echo_feedback(b);
                const float input = (dry_l + dry_r) * 0.5f;
                fxmm_delay_write(slot, slot_index, input + (slot->delay_feedback_lp * fb));
                wet_l = delayed + (side * 0.18f);
                wet_r = delayed - (side * 0.18f);
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

            case FX_MASTER_MACRO_PITCH:
            {
                fxmm_delay_write(slot, slot_index, dry_m);
                if (fabsf(pitch_ratio - 1.0f) < 0.006f)
                {
                    wet_l = dry_l;
                    wet_r = dry_r;
                    break;
                }
                const float phase_inc = fxmm_clampf(fabsf(pitch_ratio - 1.0f) * 0.42f, 0.00004f, 0.018f);
                slot->pitch_phase_a += phase_inc;
                slot->pitch_phase_b += phase_inc;
                while (slot->pitch_phase_a >= 1.0f)
                {
                    slot->pitch_phase_a -= 1.0f;
                }
                while (slot->pitch_phase_b >= 1.0f)
                {
                    slot->pitch_phase_b -= 1.0f;
                }
                const float a_gain = 1.0f - fabsf((slot->pitch_phase_a * 2.0f) - 1.0f);
                const float b_gain = 1.0f - fabsf((slot->pitch_phase_b * 2.0f) - 1.0f);
                const float shifted_a = fxmm_pitch_voice(slot, slot_index, slot->pitch_phase_a, pitch_ratio);
                const float shifted_b = fxmm_pitch_voice(slot, slot_index, slot->pitch_phase_b, pitch_ratio);
                const float denom = fxmm_clampf(a_gain + b_gain, 0.001f, 2.0f);
                const float shifted = ((shifted_a * a_gain) + (shifted_b * b_gain)) / denom;
                wet_l = shifted;
                wet_r = shifted;
                break;
            }

            case FX_MASTER_MACRO_TALK:
            {
                const float vowel = (float)((uint32_t)(a * 4.0f + 0.5f)) * 0.25f;
                wet_l = fxmm_talk_process(slot, dry_l, 0U, vowel, b);
                wet_r = fxmm_talk_process(slot, dry_r, 1U, vowel, b);
                break;
            }

            case FX_MASTER_MACRO_STUTTER:
            {
                if ((target_wet > 0.000001f) && (slot->stutter_active == 0U))
                {
                    const float len = fxmm_clampf(stutter_loop, 32.0f, 24000.0f);
                    if (slot->delay_filled >= ((uint32_t)len + 2U))
                    {
                        slot->stutter_active = 1U;
                        slot->stutter_pos = 0.0f;
                        slot->stutter_loop_samples = len;
                        slot->stutter_start = (float)slot->delay_write - len;
                    }
                }
                else if (target_wet <= 0.000001f)
                {
                    slot->stutter_active = 0U;
                }

                if (slot->stutter_active != 0U)
                {
                    const float len = fxmm_clampf(slot->stutter_loop_samples, 32.0f, 24000.0f);
                    if (slot->delay_filled >= ((uint32_t)len + 2U))
                    {
                        const float read_pos = slot->stutter_start + slot->stutter_pos;
                        const float loop = fxmm_delay_read_abs(slot_index, read_pos);
                        const float gain = fxmm_loop_xfade_gain(slot->stutter_pos, len);
                        wet_l = loop * gain;
                        wet_r = loop * gain;
                        slot->stutter_pos += stutter_rate;
                        while (slot->stutter_pos >= len)
                        {
                            slot->stutter_pos -= len;
                        }
                    }
                }
                if (slot->stutter_active == 0U)
                {
                    fxmm_delay_write(slot, slot_index, dry_m);
                }
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

            case FX_MASTER_MACRO_FREEZE:
            {
                slot->delay_samples = fxmm_smooth(slot->delay_samples, freeze_target_delay, 0.0012f);
                const float delayed = fxmm_delay_read(slot, slot_index, slot->delay_samples);
                const float target_gate = (target_wet > 0.000001f) ? 1.0f : 0.0f;
                slot->freeze_gate = fxmm_smooth(slot->freeze_gate, target_gate, 0.015f);
                const float hold_fb = 0.88f + (b * b * 0.118f);
                const float input = (dry_l + dry_r) * 0.5f;
                slot->delay_feedback_lp += (delayed - slot->delay_feedback_lp) * 0.12f;
                const float write = (input * (1.0f - slot->freeze_gate))
                        + (slot->delay_feedback_lp * hold_fb * slot->freeze_gate);
                fxmm_delay_write(slot, slot_index, write);
                wet_l = delayed;
                wet_r = delayed;
                break;
            }

            default:
                wet_l = dry_l;
                wet_r = dry_r;
                break;
        }

        left[i] = (dry_l * (1.0f - slot->wet)) + (wet_l * slot->wet);
        right[i] = (dry_r * (1.0f - slot->wet)) + (wet_r * slot->wet);
        if (slot->wet > 0.000001f)
        {
            left[i] = fxmm_clampf(left[i], -1.20f, 1.20f);
            right[i] = fxmm_clampf(right[i], -1.20f, 1.20f);
        }
    }
}

void fx_master_macro_init(float sample_rate)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_sample_rate = (sample_rate > 1000.0f) ? sample_rate : FX_MASTER_MACRO_SAMPLE_RATE_DEFAULT;
}

void fx_master_macro_process_block(float *left, float *right, uint32_t frames)
{
    uint8_t track = 0U;
    if ((left == NULL) || (right == NULL) || (frames == 0U) || (fxmm_find_master_fx_track(&track) == 0U))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    const track_tone_sound_state_t *state = track_tone_sound_state_get_const(track);
    if (state == NULL)
    {
        return;
    }

    const float bpm_milli = (float)fxmm_get_bpm_milli();
    for (uint8_t slot = 0U; slot < FX_MASTER_MACRO_SLOT_COUNT; ++slot)
    {
        const uint8_t type = fxmm_u7(state->master_fx.type[slot]);
        fxmm_process_slot(&g_slots[slot],
                          slot,
                          type,
                          state->master_fx.level[slot],
                          state->master_fx.macro_a[slot],
                          state->master_fx.macro_b[slot],
                          left,
                          right,
                          frames,
                          bpm_milli);
    }
}
