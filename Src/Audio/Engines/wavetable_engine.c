#include "Audio/Engines/wavetable_engine.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Audio/synth_waveform_audio.h"
#include "Audio/audio_shared_memory.h"
#include "Audio/audio_wavetable_registry.h"
#include "Sampler/wavetable_config.h"
#include "Platform/memory_layout.h"

#define WAVE_DEFAULT_NOTE          60U
#define WAVE_SAMPLE_RATE           48000.0f
#define WAVE_A4_NOTE               69.0f
#define WAVE_A4_FREQ               440.0f
#define WAVE_FRAME_FRAC_EPS        0.000001f
#define WAVE_PHASE_INDEX_BITS      10U
#define WAVE_PHASE_FRAC_BITS       (32U - WAVE_PHASE_INDEX_BITS)
#define WAVE_PHASE_FRAC_MASK       ((1UL << WAVE_PHASE_FRAC_BITS) - 1UL)
#define WAVE_PHASE_FRAC_TO_FLOAT   (1.0f / (float)(1UL << WAVE_PHASE_FRAC_BITS))
#define WAVE_PHASE_SAMPLE_ROUND    (1UL << (WAVE_PHASE_FRAC_BITS - 1U))
#define WAVE_PHASE_SCALE           4294967296.0
#define WAVE_GAIN_SILENCE_EPS      0.00001f
#define BRICK6_WAVE_OUTPUT_GAIN    0.42169650f
#define WAVE_OUTPUT_TRIM           0.30f

typedef struct wave_osc_block_ctx_t wave_osc_block_ctx_t;
enum
{
    WAVE_CONT_POS_BASE = 0,
    WAVE_CONT_START_BASE = WAVE_CONT_POS_BASE + BRICK6_WAVE_OSC_COUNT,
    WAVE_CONT_LEN_BASE = WAVE_CONT_START_BASE + BRICK6_WAVE_OSC_COUNT,
    WAVE_CONT_VOLUME = WAVE_CONT_LEN_BASE + BRICK6_WAVE_OSC_COUNT,
    WAVE_CONT_BALANCE,
    WAVE_CONT_TUNE,
    WAVE_CONT_DETUNE,
    WAVE_CONT_COUNT
};
typedef struct
{
    brick6_wave_runtime_osc_t osc[BRICK6_WAVE_OSC_COUNT];
    brick6_wave_runtime_voice_t voice;
    float volume;
    float volume_current;
    float balance;
    float tune_semitones;
    float detune_semitones;
    uint32_t config_version;
    uint32_t synced_config_version;
    uint32_t continuous_epoch;
    uint32_t continuous_version[WAVE_CONT_COUNT];
} brick6_wave_runtime_instance_t;

struct wave_osc_block_ctx_t
{
    brick6_wave_runtime_osc_t *osc;
    const float *frame0_data;
    const float *frame1_data;
    uint32_t cycle_sample_count;
    uint32_t phase_shift;
    uint32_t phase_mask;
    float phase_to_float;
    float frame_frac;
    float balance_gain_current;
    float balance_gain_step;
    float pos_smoothed;
    double phase_inc_current;
    double phase_inc_step;
    uint32_t phase;
    uint32_t phase_inc_value;
    uint32_t start_phase;
    uint32_t length_phase;
    uint8_t phase_inc_ramping;
    uint8_t frame_interp;
};

AUDIO_HOT static brick6_wave_runtime_instance_t g_wave_runtime[BRICK6_WAVE_MAX_INSTANCES];
AUDIO_HOT static brick6_wave_runtime_instance_t
    g_wave_poly_runtime[BRICK6_WAVE_VOICE_INSTANCE_COUNT - BRICK6_WAVE_MAX_INSTANCES];
static uint32_t g_wave_continuous_version;

static float wave_clampf(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static uint32_t wave_start_to_phase(float value)
{
    const double scaled = (double)wave_clampf(value, 0.0f, 1.0f) * WAVE_PHASE_SCALE;
    return (scaled >= WAVE_PHASE_SCALE) ? UINT32_MAX : (uint32_t)(scaled + 0.5);
}

static uint32_t wave_length_to_phase(float value)
{
    const double scaled = (double)wave_clampf(value, 0.01f, 1.0f) * WAVE_PHASE_SCALE;
    return (scaled >= WAVE_PHASE_SCALE) ? 0U : (uint32_t)(scaled + 0.5);
}

static inline __attribute__((always_inline))
uint32_t wave_remap_read_phase(const wave_osc_block_ctx_t *ctx,
                               uint32_t carrier_phase)
{
    const uint32_t length_phase = ctx->length_phase;
    if ((ctx->start_phase == 0U) && (length_phase == 0U)) return carrier_phase;
    const uint32_t scaled_phase = (length_phase != 0U)
        ? (uint32_t)(((uint64_t)carrier_phase * (uint64_t)length_phase) >> 32U)
        : 0U;
    return ctx->start_phase + scaled_phase;
}

static brick6_wave_runtime_instance_t *wave_get_instance_mut(uint8_t instance_id)
{
    if (instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT)
    {
        return NULL;
    }
    return (instance_id < BRICK6_WAVE_MAX_INSTANCES)
        ? &g_wave_runtime[instance_id]
        : &g_wave_poly_runtime[instance_id - BRICK6_WAVE_MAX_INSTANCES];
}

static const brick6_wave_runtime_instance_t *wave_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT)
    {
        return NULL;
    }
    return (instance_id < BRICK6_WAVE_MAX_INSTANCES)
        ? &g_wave_runtime[instance_id]
        : &g_wave_poly_runtime[instance_id - BRICK6_WAVE_MAX_INSTANCES];
}

static void wave_touch_config(brick6_wave_runtime_instance_t *instance)
{
    uint32_t version = instance->config_version + 1U;
    instance->config_version = (version != 0U) ? version : 1U;
}

static void wave_touch_continuous(brick6_wave_runtime_instance_t *instance,
                                  uint8_t param)
{
    if ((instance == NULL) || (param >= WAVE_CONT_COUNT)) return;
    g_wave_continuous_version++;
    if (g_wave_continuous_version == 0U) g_wave_continuous_version = 1U;
    instance->continuous_version[param] = g_wave_continuous_version;
    instance->continuous_epoch = g_wave_continuous_version;
}

static uint8_t wave_build_hot_table(
    const audio_wavetable_descriptor_t *table,
    brick6_wave_hot_table_t *out)
{
    if (out == NULL)
    {
        return 0U;
    }
    memset(out, 0, sizeof(*out));
    if ((table == NULL)
        || (table->generation == 0U)
        || (table->wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (table->frame_count == 0U)
        || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (table->band_count > WAVETABLE_MIPMAP_MAX_BANDS))
    {
        return 0U;
    }
    out->generation = table->generation;
    out->frame_count = table->frame_count;
    out->wavetable_slot = table->wavetable_slot;
    if (table->band_count == 0U)
    {
        const float *const data = (const float *)audio_shared_memory_consume(
            &table->base_data);
        if (data == NULL) return 0U;
        out->band_count = 1U;
        out->bands[0].data = data;
        out->bands[0].max_phase_increment = UINT32_MAX;
    }
    else
    {
        out->band_count = (uint8_t)table->band_count;
        for (uint8_t i = 0U; i < out->band_count; ++i)
        {
            const audio_wavetable_band_t *const src = &table->bands[i];
            const uint32_t expected_magnitude = WAVE_PHASE_INDEX_BITS - i;
            const float *const data = (const float *)audio_shared_memory_consume(
                &src->data);
            if ((data == NULL)
                || (src->cycle_magnitude != expected_magnitude)
                || (src->cycle_sample_count != (1UL << expected_magnitude)))
            {
                memset(out, 0, sizeof(*out));
                return 0U;
            }
            out->bands[i].data = data;
            out->bands[i].max_phase_increment = src->max_phase_increment;
        }
    }
    out->valid = 1U;
    return 1U;
}

static float wave_pitch_ratio(float semitones)
{
    float octaves = semitones * (1.0f / 12.0f);
    int32_t octave = (int32_t)octaves;
    if ((octaves < 0.0f) && ((float)octave != octaves)) --octave;
    if (octave < -30) octave = -30;
    if (octave > 30) octave = 30;
    const float x = octaves - (float)octave;
    const float fraction = 1.0f + x * (0.6931471806f
        + x * (0.2402265070f + x * (0.0555041087f
        + x * (0.0096181291f + x * 0.0013333558f))));
    union { uint32_t u; float f; } power = {
        .u = (uint32_t)(octave + 127) << 23U
    };
    return power.f * fraction;
}

static uint32_t wave_note_to_phase_inc(float note, float tune_semitones)
{
    const float semitone_from_a4 = (note + tune_semitones) - WAVE_A4_NOTE;
    const float hz = WAVE_A4_FREQ * wave_pitch_ratio(semitone_from_a4);
    float cycles_per_sample = hz * (1.0f / WAVE_SAMPLE_RATE);
    cycles_per_sample -= floorf(cycles_per_sample);
    if (cycles_per_sample < 0.0f)
    {
        cycles_per_sample += 1.0f;
    }

    const double scaled = (double)cycles_per_sample * WAVE_PHASE_SCALE;
    if (scaled >= 4294967295.5)
    {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(scaled + 0.5);
}

static void wave_advance_pos_silent_block(brick6_wave_runtime_osc_t *osc,
                                          uint32_t frames)
{
    if ((osc == NULL) || (frames == 0U))
    {
        return;
    }

    const float target_pos = wave_clampf(osc->pos, 0.0f, 1.0f);
    (void)frames;
    osc->pos_smoothed = target_pos;
}

static void wave_snap_pos(brick6_wave_runtime_osc_t *osc)
{
    if (osc != NULL)
    {
        osc->pos_smoothed = wave_clampf(osc->pos, 0.0f, 1.0f);
    }
}

static void wave_update_pitch(brick6_wave_runtime_instance_t *instance, uint8_t osc_index)
{
    if ((instance == NULL) || (osc_index >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    const float note = ((instance->voice.has_active_note != 0U)
            || (instance->voice.velocity > 0.0f))
        ? (float)instance->voice.active_note
        : (float)WAVE_DEFAULT_NOTE;
    const float relative = (osc_index == 1U) ? instance->detune_semitones : 0.0f;
    instance->osc[osc_index].phase_inc =
        wave_note_to_phase_inc(note, instance->tune_semitones + relative);
}

static void wave_set_balance_gains(brick6_wave_runtime_instance_t *instance,
                                   float balance)
{
    const float clamped = wave_clampf(balance, -1.0f, 1.0f);
    instance->balance = clamped;
    instance->osc[0].balance_gain = (clamped <= 0.0f) ? 1.0f : (1.0f - clamped);
    instance->osc[1].balance_gain = (clamped >= 0.0f) ? 1.0f : (1.0f + clamped);
}

static void wave_reset_osc(brick6_wave_runtime_osc_t *osc)
{
    memset(osc, 0, sizeof(*osc));
    osc->table_global_slot = 0U;
    osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    osc->table_generation = 0U;
    osc->balance_gain = 1.0f;
    osc->balance_gain_current = 1.0f;
    osc->pos = 0.0f;
    osc->pos_smoothed = 0.0f;
    osc->start_phase = 0U;
    osc->length_phase = 0U;
    osc->phase = 0U;
    osc->phase_inc = wave_note_to_phase_inc((float)WAVE_DEFAULT_NOTE, 0.0f);
    osc->phase_inc_current = osc->phase_inc;
}

static void wave_reset_instance(brick6_wave_runtime_instance_t *instance)
{
    if (instance == NULL)
    {
        return;
    }
    memset(instance, 0, sizeof(*instance));
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        wave_reset_osc(&instance->osc[osc]);
    }
    instance->volume = 1.0f;
    instance->volume_current = 1.0f;
    instance->balance = 0.0f;
    instance->tune_semitones = 0.0f;
    instance->detune_semitones = 0.0f;
    wave_set_balance_gains(instance, 0.0f);
    instance->config_version = 1U;
}

static inline __attribute__((always_inline))
float wave_read_frame_sample(const float *frame_data,
                             uint32_t phase,
                             uint32_t cycle_sample_count,
                             uint32_t phase_shift,
                             uint32_t phase_mask,
                             float phase_to_float)
{
    const uint32_t i0 = phase >> phase_shift;
    const uint32_t i1 = (i0 + 1U) & (cycle_sample_count - 1U);
    const uint32_t frac_q = phase & phase_mask;
    const float frac = (float)frac_q * phase_to_float;
    const float a = frame_data[i0];
    return a + ((frame_data[i1] - a) * frac);
}

static inline __attribute__((always_inline))
void wave_advance_phase(wave_osc_block_ctx_t *ctx)
{
    if (ctx->phase_inc_ramping != 0U)
    {
        ctx->phase_inc_current += ctx->phase_inc_step;
        ctx->phase_inc_value = (uint32_t)(ctx->phase_inc_current + 0.5);
    }
    else
    {
        ctx->phase_inc_value = ctx->osc->phase_inc;
    }
    ctx->phase += ctx->phase_inc_value;
}

static inline __attribute__((always_inline))
void wave_select_frame_from_pos(wave_osc_block_ctx_t *ctx,
                                const float *data,
                                uint32_t frame_count,
                                uint32_t cycle_stride,
                                float smoothed_pos)
{
    const float max_frame = (frame_count > 1U) ? (float)(frame_count - 1U) : 0.0f;
    const float frame_f = smoothed_pos * max_frame;
    uint32_t frame0 = (uint32_t)frame_f;
    if (frame0 >= frame_count)
    {
        frame0 = frame_count - 1U;
    }
    uint32_t frame1 = frame0 + 1U;
    if (frame1 >= frame_count)
    {
        frame1 = frame0;
    }

    float frame_frac = frame_f - (float)frame0;
    if (((1.0f - frame_frac) <= WAVE_FRAME_FRAC_EPS) && (frame1 != frame0))
    {
        frame0 = frame1;
        frame_frac = 0.0f;
    }
    else if (frame_frac <= WAVE_FRAME_FRAC_EPS)
    {
        frame_frac = 0.0f;
    }

    ctx->frame_frac = frame_frac;
    ctx->frame0_data = &data[frame0 * cycle_stride];
    ctx->frame1_data = &data[frame1 * cycle_stride];
    ctx->frame_interp = ((frame1 != frame0) && (frame_frac != 0.0f)) ? 1U : 0U;
}

static uint8_t wave_prepare_osc_block(brick6_wave_runtime_osc_t *osc,
                                      wave_osc_block_ctx_t *ctx,
                                      uint32_t frames,
                                      uint8_t source_required)
{
    memset(ctx, 0, sizeof(*ctx));
    if (osc == NULL)
    {
        return 0U;
    }

    ctx->osc = osc;
    ctx->balance_gain_current = osc->balance_gain_current;
    ctx->balance_gain_step = (frames != 0U)
        ? ((osc->balance_gain - osc->balance_gain_current) / (float)frames) : 0.0f;
    ctx->phase_inc_current = (double)osc->phase_inc_current;
    ctx->phase = osc->phase;
    ctx->start_phase = osc->start_phase;
    if ((osc->start_phase == 0U) && (osc->length_phase == 0U))
    {
        ctx->length_phase = 0U;
    }
    else
    {
        const uint32_t available = UINT32_MAX - osc->start_phase;
        const uint32_t requested = (osc->length_phase == 0U)
            ? UINT32_MAX : osc->length_phase;
        ctx->length_phase = (requested < available) ? requested : available;
    }
    ctx->phase_inc_value = osc->phase_inc_current;
    ctx->phase_inc_step = (frames != 0U)
        ? (((double)osc->phase_inc - (double)osc->phase_inc_current) / (double)frames)
        : 0.0;
    ctx->phase_inc_ramping = (osc->phase_inc_current != osc->phase_inc) ? 1U : 0U;
    if ((osc->balance_gain <= WAVE_GAIN_SILENCE_EPS)
        && (osc->balance_gain_current <= WAVE_GAIN_SILENCE_EPS)
        && (source_required == 0U))
    {
        wave_advance_pos_silent_block(osc, frames);
        osc->balance_gain_current = osc->balance_gain;
        osc->phase_inc_current = osc->phase_inc;
        return 0U;
    }

    const brick6_wave_hot_table_t *const mipmap = &osc->hot_table;
    if (mipmap->valid == 0U)
    {
        wave_advance_pos_silent_block(osc, frames);
        return 0U;
    }
    const uint32_t phase_inc = osc->phase_inc;
    uint8_t selected = osc->mipmap_band;
    if ((osc->mipmap_phase_inc != phase_inc)
        || (selected >= mipmap->band_count))
    {
        selected = (uint8_t)(mipmap->band_count - 1U);
        for (uint8_t i = 0U; i < mipmap->band_count; ++i)
        {
            if (phase_inc <= mipmap->bands[i].max_phase_increment)
            {
                selected = i;
                break;
            }
        }
        osc->mipmap_band = selected;
        osc->mipmap_phase_inc = phase_inc;
    }
    const brick6_wave_hot_band_t *const band = &mipmap->bands[selected];
    const float *const data = band->data;
    const uint32_t cycle_magnitude = WAVE_PHASE_INDEX_BITS - selected;
    ctx->cycle_sample_count = 1UL << cycle_magnitude;
    const uint32_t cycle_stride = (ctx->cycle_sample_count == 8U)
        ? 16U : ctx->cycle_sample_count;
    ctx->phase_shift = 32U - cycle_magnitude;
    ctx->phase_mask = (1UL << ctx->phase_shift) - 1UL;
    ctx->phase_to_float = 1.0f / (float)(1UL << ctx->phase_shift);
    const float target_pos = wave_clampf(osc->pos, 0.0f, 1.0f);
    ctx->pos_smoothed = target_pos;
    osc->pos_smoothed = target_pos;
    wave_select_frame_from_pos(ctx, data, mipmap->frame_count, cycle_stride, target_pos);
    return 1U;
}

static inline __attribute__((always_inline))
float wave_read_osc_sample_ctx(const wave_osc_block_ctx_t *ctx,
                               uint32_t read_phase)
{
    float out = wave_read_frame_sample(ctx->frame0_data,
                                       read_phase,
                                       ctx->cycle_sample_count,
                                       ctx->phase_shift,
                                       ctx->phase_mask,
                                       ctx->phase_to_float);
    if (ctx->frame_interp != 0U)
    {
        const float b = wave_read_frame_sample(ctx->frame1_data,
                                               read_phase,
                                               ctx->cycle_sample_count,
                                               ctx->phase_shift,
                                               ctx->phase_mask,
                                               ctx->phase_to_float);
        out += (b - out) * ctx->frame_frac;
    }
    return out;
}

static inline __attribute__((always_inline))
void wave_read_two_osc_morph_samples(const wave_osc_block_ctx_t *ctx0,
                                     uint32_t phase0,
                                     const wave_osc_block_ctx_t *ctx1,
                                     uint32_t phase1,
                                     float phase_to_float0,
                                     float phase_to_float1,
                                     float frame_frac0,
                                     float frame_frac1,
                                     float *out0,
                                     float *out1)
{
    const uint32_t i00 = phase0 >> ctx0->phase_shift;
    const uint32_t i10 = phase1 >> ctx1->phase_shift;
    const uint32_t i01 = (i00 + 1U) & (ctx0->cycle_sample_count - 1U);
    const uint32_t i11 = (i10 + 1U) & (ctx1->cycle_sample_count - 1U);
    const float frac0 = (float)(phase0 & ctx0->phase_mask) * phase_to_float0;
    const float frac1 = (float)(phase1 & ctx1->phase_mask) * phase_to_float1;

    const float a00 = ctx0->frame0_data[i00];
    const float a10 = ctx1->frame0_data[i10];
    const float a01 = ctx0->frame0_data[i01];
    const float a11 = ctx1->frame0_data[i11];
    float sample0 = a00 + ((a01 - a00) * frac0);
    float sample1 = a10 + ((a11 - a10) * frac1);

    const float b00 = ctx0->frame1_data[i00];
    const float b10 = ctx1->frame1_data[i10];
    const float b01 = ctx0->frame1_data[i01];
    const float b11 = ctx1->frame1_data[i11];
    const float frame1_sample0 = b00 + ((b01 - b00) * frac0);
    const float frame1_sample1 = b10 + ((b11 - b10) * frac1);
    sample0 += (frame1_sample0 - sample0) * frame_frac0;
    sample1 += (frame1_sample1 - sample1) * frame_frac1;
    *out0 = sample0;
    *out1 = sample1;
}

static ITCM_TEXT __attribute__((optimize("no-associative-math")))
void wave_render_two_osc_morph_stable_block(
    wave_osc_block_ctx_t *restrict ctx0,
    wave_osc_block_ctx_t *restrict ctx1,
    float *restrict out_mono,
    uint32_t frames,
    float volume_current,
    float volume_step,
    float output_scale)
{
    uint32_t phase0 = ctx0->phase;
    uint32_t phase1 = ctx1->phase;
    const uint32_t phase_inc0 = ctx0->osc->phase_inc;
    const uint32_t phase_inc1 = ctx1->osc->phase_inc;
    float gain0 = ctx0->balance_gain_current;
    float gain1 = ctx1->balance_gain_current;
    const float gain_step0 = ctx0->balance_gain_step;
    const float gain_step1 = ctx1->balance_gain_step;
    const float phase_to_float0 = ctx0->phase_to_float;
    const float phase_to_float1 = ctx1->phase_to_float;
    const float frame_frac0 = ctx0->frame_frac;
    const float frame_frac1 = ctx1->frame_frac;

    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        float raw0;
        float raw1;
        wave_read_two_osc_morph_samples(ctx0, phase0, ctx1, phase1,
                                        phase_to_float0, phase_to_float1,
                                        frame_frac0, frame_frac1,
                                        &raw0, &raw1);
        phase0 += phase_inc0;
        phase1 += phase_inc1;
        gain0 += gain_step0;
        gain1 += gain_step1;
        float mono = raw0 * gain0;
        __asm__ volatile ("" : "+t" (mono));
        mono += raw1 * gain1;
        volume_current += volume_step;
        out_mono[frame] = mono * output_scale * volume_current;
    }

    ctx0->phase = phase0;
    ctx1->phase = phase1;
    ctx0->phase_inc_value = phase_inc0;
    ctx1->phase_inc_value = phase_inc1;
    ctx0->balance_gain_current = gain0;
    ctx1->balance_gain_current = gain1;
}

static inline __attribute__((always_inline))
float wave_render_osc_sample_stable_ctx(wave_osc_block_ctx_t *ctx,
                                        uint32_t read_phase)
{
    const float out = wave_read_osc_sample_ctx(ctx, read_phase);
    wave_advance_phase(ctx);
    ctx->balance_gain_current += ctx->balance_gain_step;
    return out;
}

void brick6_wave_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_WAVE_VOICE_INSTANCE_COUNT; ++instance)
    {
        wave_reset_instance(wave_get_instance_mut(instance));
    }
}

void brick6_wave_runtime_reset_instance(uint8_t instance_id)
{
    wave_reset_instance(wave_get_instance_mut(instance_id));
}

void brick6_wave_runtime_set_osc_table_wavetable(uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot)
{
    brick6_wave_runtime_set_osc_table_wavetable_generation(
        instance_id, osc, wavetable_slot, 0U);
}

void brick6_wave_runtime_set_osc_table_wavetable_generation(
    uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot,
    uint32_t generation)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    brick6_wave_runtime_osc_t *const target = &instance->osc[osc];
    const uint16_t previous_slot = target->table_wavetable_slot;
    const uint32_t previous_generation = target->table_generation;
    target->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    target->table_generation = 0U;
    target->table_global_slot = UINT16_MAX;
    memset(&target->hot_table, 0, sizeof(target->hot_table));
    audio_wavetable_descriptor_t table;
    if (audio_wavetable_registry_resolve(wavetable_slot, generation, &table) != 0U)
    {
        brick6_wave_hot_table_t hot_table;
        if (wave_build_hot_table(&table, &hot_table) != 0U)
        {
            target->hot_table = hot_table;
            target->table_wavetable_slot = wavetable_slot;
            target->table_generation = table.generation;
            target->table_global_slot = table.global_slot;
            if ((target->table_wavetable_slot != previous_slot)
                    || (target->table_generation != previous_generation))
            {
                target->mipmap_band = 0U;
                target->mipmap_phase_inc = 0U;
                wave_snap_pos(target);
            }
        }
    }
    if ((target->table_wavetable_slot != previous_slot)
            || (target->table_generation != previous_generation)) wave_touch_config(instance);
}

void brick6_wave_runtime_set_osc_pos(uint8_t instance_id, uint8_t osc, float pos)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    const float next = wave_clampf(pos, 0.0f, 1.0f);
    if (instance->osc[osc].pos == next) return;
    instance->osc[osc].pos = next;
    wave_touch_continuous(instance, (uint8_t)(WAVE_CONT_POS_BASE + osc));
}

void brick6_wave_runtime_set_osc_start(uint8_t instance_id, uint8_t osc, float start)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT)) return;
    const uint32_t next = wave_start_to_phase(start);
    if (instance->osc[osc].start_phase == next) return;
    instance->osc[osc].start_phase = next;
    wave_touch_continuous(instance, (uint8_t)(WAVE_CONT_START_BASE + osc));
}

void brick6_wave_runtime_set_osc_len(uint8_t instance_id, uint8_t osc, float len)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT)) return;
    const uint32_t next = wave_length_to_phase(len);
    if (instance->osc[osc].length_phase == next) return;
    instance->osc[osc].length_phase = next;
    wave_touch_continuous(instance, (uint8_t)(WAVE_CONT_LEN_BASE + osc));
}

void brick6_wave_runtime_set_volume(uint8_t instance_id, float volume)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL) return;
    const float next = wave_clampf(volume, 0.0f, 1.0f);
    if (instance->volume == next) return;
    instance->volume = next;
    wave_touch_continuous(instance, WAVE_CONT_VOLUME);
}

void brick6_wave_runtime_set_balance(uint8_t instance_id, float balance)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL) return;
    const float next = wave_clampf(balance, -1.0f, 1.0f);
    if (instance->balance == next) return;
    wave_set_balance_gains(instance, next);
    wave_touch_continuous(instance, WAVE_CONT_BALANCE);
}

void brick6_wave_runtime_set_tune(uint8_t instance_id, float semitones)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL) return;
    const float next = wave_clampf(semitones, -60.0f, 60.0f);
    if (instance->tune_semitones == next) return;
    instance->tune_semitones = next;
    wave_update_pitch(instance, 0U);
    wave_update_pitch(instance, 1U);
    wave_touch_continuous(instance, WAVE_CONT_TUNE);
}

void brick6_wave_runtime_set_detune(uint8_t instance_id, float semitones)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL) return;
    const float next = wave_clampf(semitones, -24.0f, 24.0f);
    if (instance->detune_semitones == next) return;
    instance->detune_semitones = next;
    wave_update_pitch(instance, 1U);
    wave_touch_continuous(instance, WAVE_CONT_DETUNE);
}

void brick6_wave_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->voice.active_note = note;
    instance->voice.has_active_note = 1U;
    instance->voice.gate = 1U;
    instance->voice.trigger = 1U;
    instance->voice.velocity = wave_clampf((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        instance->osc[osc].phase = 0U;
        wave_snap_pos(&instance->osc[osc]);
        wave_update_pitch(instance, osc);
        instance->osc[osc].phase_inc_current = instance->osc[osc].phase_inc;
    }
}

void brick6_wave_runtime_initialize_held_note(uint8_t instance_id,
                                              uint8_t note,
                                              uint8_t velocity)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL) return;
    instance->voice.active_note = note;
    instance->voice.has_active_note = 1U;
    instance->voice.gate = 1U;
    instance->voice.trigger = 0U;
    instance->voice.velocity = wave_clampf(
        (float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        wave_snap_pos(&instance->osc[osc]);
        wave_update_pitch(instance, osc);
        instance->osc[osc].phase_inc_current = instance->osc[osc].phase_inc;
    }
}

void brick6_wave_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    if ((instance->voice.has_active_note != 0U) && (instance->voice.active_note == note))
    {
        /* Amplitude release is owned by the track mixer VCA; keep velocity for that tail. */
        instance->voice.gate = 0U;
        instance->voice.has_active_note = 0U;
    }
}

void brick6_wave_runtime_all_notes_off(uint8_t instance_id)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance == NULL)
    {
        return;
    }
    instance->voice.gate = 0U;
    instance->voice.has_active_note = 0U;
    instance->voice.trigger = 0U;
    instance->voice.velocity = 0.0f;
}

void brick6_wave_runtime_stop_wavetable_slot(uint16_t wavetable_slot,
                                            uint32_t generation)
{
    for (uint8_t instance_id = 0U;
         instance_id < BRICK6_WAVE_VOICE_INSTANCE_COUNT; ++instance_id)
    {
        brick6_wave_runtime_instance_t *const instance =
            wave_get_instance_mut(instance_id);
        for (uint8_t osc_id = 0U; osc_id < BRICK6_WAVE_OSC_COUNT; ++osc_id)
        {
            brick6_wave_runtime_osc_t *const osc = &instance->osc[osc_id];
            if ((osc->table_wavetable_slot == wavetable_slot)
                && (osc->table_generation == generation))
            {
                osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
                osc->table_global_slot = UINT16_MAX;
                osc->table_generation = 0U;
                osc->mipmap_band = 0U;
                osc->mipmap_phase_inc = 0U;
                memset(&osc->hot_table, 0, sizeof(osc->hot_table));
                wave_touch_config(instance);
            }
        }
    }
}

void brick6_wave_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

void brick6_wave_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance)
{
    const brick6_wave_runtime_instance_t *const src =
        wave_get_instance(track_instance);
    brick6_wave_runtime_instance_t *const dst =
        wave_get_instance_mut(voice_instance);
    if ((src == NULL) || (dst == NULL) || (src == dst))
    {
        return;
    }
    if ((dst->synced_config_version == src->config_version)
            && (dst->continuous_epoch == src->continuous_epoch)) return;
    const uint8_t full = (dst->synced_config_version != src->config_version) ? 1U : 0U;
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        if (full != 0U)
        {
            brick6_wave_runtime_osc_t *const voice_osc = &dst->osc[osc];
            const brick6_wave_runtime_osc_t *const track_osc = &src->osc[osc];
            /* Config sync may project shared table identity, but phase, pitch,
             * smoothing and gain history remain owned by the active voice. */
            const uint8_t table_changed = (uint8_t)(
                (voice_osc->table_global_slot != track_osc->table_global_slot)
                || (voice_osc->table_wavetable_slot != track_osc->table_wavetable_slot)
                || (voice_osc->table_generation != track_osc->table_generation));
            voice_osc->table_global_slot = track_osc->table_global_slot;
            voice_osc->table_wavetable_slot = track_osc->table_wavetable_slot;
            voice_osc->table_generation = track_osc->table_generation;
            voice_osc->hot_table = track_osc->hot_table;
            if (table_changed != 0U)
            {
                voice_osc->mipmap_band = 0U;
                voice_osc->mipmap_phase_inc = 0U;
                wave_snap_pos(voice_osc);
            }
        }
        const uint8_t pos_param = (uint8_t)(WAVE_CONT_POS_BASE + osc);
        if ((full != 0U) || (dst->continuous_version[pos_param]
                != src->continuous_version[pos_param]))
        {
            dst->osc[osc].pos = src->osc[osc].pos;
            dst->continuous_version[pos_param] = src->continuous_version[pos_param];
        }
        const uint8_t start_param = (uint8_t)(WAVE_CONT_START_BASE + osc);
        if ((full != 0U) || (dst->continuous_version[start_param]
                != src->continuous_version[start_param]))
        {
            dst->osc[osc].start_phase = src->osc[osc].start_phase;
            dst->continuous_version[start_param] = src->continuous_version[start_param];
        }
        const uint8_t len_param = (uint8_t)(WAVE_CONT_LEN_BASE + osc);
        if ((full != 0U) || (dst->continuous_version[len_param]
                != src->continuous_version[len_param]))
        {
            dst->osc[osc].length_phase = src->osc[osc].length_phase;
            dst->continuous_version[len_param] = src->continuous_version[len_param];
        }
    }
    if ((full != 0U) || (dst->continuous_version[WAVE_CONT_VOLUME]
            != src->continuous_version[WAVE_CONT_VOLUME]))
    {
        dst->volume = src->volume;
        dst->continuous_version[WAVE_CONT_VOLUME] = src->continuous_version[WAVE_CONT_VOLUME];
    }
    if ((full != 0U) || (dst->continuous_version[WAVE_CONT_BALANCE]
            != src->continuous_version[WAVE_CONT_BALANCE]))
    {
        wave_set_balance_gains(dst, src->balance);
        dst->continuous_version[WAVE_CONT_BALANCE] = src->continuous_version[WAVE_CONT_BALANCE];
    }
    if ((full != 0U) || (dst->continuous_version[WAVE_CONT_TUNE]
            != src->continuous_version[WAVE_CONT_TUNE]))
    {
        dst->tune_semitones = src->tune_semitones;
        wave_update_pitch(dst, 0U);
        wave_update_pitch(dst, 1U);
        dst->continuous_version[WAVE_CONT_TUNE] = src->continuous_version[WAVE_CONT_TUNE];
    }
    if ((full != 0U) || (dst->continuous_version[WAVE_CONT_DETUNE]
            != src->continuous_version[WAVE_CONT_DETUNE]))
    {
        dst->detune_semitones = src->detune_semitones;
        wave_update_pitch(dst, 1U);
        dst->continuous_version[WAVE_CONT_DETUNE] = src->continuous_version[WAVE_CONT_DETUNE];
    }
    if (full != 0U) dst->synced_config_version = src->config_version;
    dst->continuous_epoch = src->continuous_epoch;
}

uint8_t brick6_wave_runtime_prepare_block(uint8_t instance_id,
                                          uint32_t frames,
                                          uint8_t downstream_source_required)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (frames == 0U))
    {
        return 0U;
    }

    if (instance->voice.velocity <= 0.0f)
    {
        return 0U;
    }

    if ((instance->voice.gate == 0U) && (downstream_source_required == 0U))
    {
        for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
        {
            brick6_wave_runtime_osc_t *const voice_osc = &instance->osc[osc];
            voice_osc->phase += voice_osc->phase_inc * frames;
            voice_osc->phase_inc_current = voice_osc->phase_inc;
            voice_osc->balance_gain_current = voice_osc->balance_gain;
            wave_advance_pos_silent_block(voice_osc, frames);
        }
        instance->volume_current = instance->volume;
        return 0U;
    }

    return 1U;
}

static ITCM_TEXT uint8_t wave_render_instance_block(uint8_t instance_id,
                                                     float *out_mono,
                                                     uint32_t frames,
                                                     uint8_t waveform_mask)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (out_mono == NULL) || (frames == 0U)
            || (instance->voice.velocity <= 0.0f))
    {
        return 0U;
    }

    wave_osc_block_ctx_t osc_ctx[BRICK6_WAVE_OSC_COUNT];
    uint8_t prepared[BRICK6_WAVE_OSC_COUNT] = {0U, 0U};
    uint8_t prepared_count = 0U;
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        prepared[osc] = wave_prepare_osc_block(
            &instance->osc[osc], &osc_ctx[osc], frames,
            (uint8_t)((waveform_mask & (uint8_t)(1U << osc)) != 0U));
        prepared_count += prepared[osc];
    }
    if (prepared_count == 0U)
    {
        return 0U;
    }

    float volume_current = instance->volume_current;
    const float volume_step = (instance->volume - volume_current) / (float)frames;
    const float output_scale = instance->voice.velocity
        * WAVE_OUTPUT_TRIM * BRICK6_WAVE_OUTPUT_GAIN;

    const uint8_t morph_mask = (uint8_t)(
        (osc_ctx[0].frame_interp != 0U ? 1U : 0U)
        | (osc_ctx[1].frame_interp != 0U ? 2U : 0U));
    if ((morph_mask == 3U)
        && (prepared_count == BRICK6_WAVE_OSC_COUNT)
        && (waveform_mask == 0U)
        && (osc_ctx[0].phase_inc_ramping == 0U)
        && (osc_ctx[1].phase_inc_ramping == 0U)
        && (osc_ctx[0].start_phase == 0U)
        && (osc_ctx[0].length_phase == 0U)
        && (osc_ctx[1].start_phase == 0U)
        && (osc_ctx[1].length_phase == 0U))
    {
        wave_render_two_osc_morph_stable_block(
            &osc_ctx[0], &osc_ctx[1], out_mono, frames,
            volume_current, volume_step, output_scale);
        volume_current = instance->volume;
        goto wave_render_commit;
    }

    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        float mono = 0.0f;
        for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
        {
            if (prepared[osc] == 0U)
            {
                continue;
            }
            const uint32_t carrier_phase = osc_ctx[osc].phase;
            const uint32_t read_phase = wave_remap_read_phase(
                &osc_ctx[osc], carrier_phase);
            const float raw = wave_render_osc_sample_stable_ctx(
                &osc_ctx[osc], read_phase);
            if ((waveform_mask & (uint8_t)(1U << osc)) != 0U)
            {
                synth_waveform_audio_capture_sample(
                    instance_id, osc, carrier_phase, raw);
            }
            mono += raw * osc_ctx[osc].balance_gain_current;
        }
        volume_current += volume_step;
        out_mono[frame] = mono * volume_current * output_scale;
    }

wave_render_commit:
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        if (prepared[osc] == 0U)
        {
            continue;
        }
        instance->osc[osc].phase = osc_ctx[osc].phase;
        instance->osc[osc].phase_inc_current = osc_ctx[osc].phase_inc_value;
        instance->osc[osc].balance_gain_current = osc_ctx[osc].balance_gain_current;
        instance->osc[osc].pos_smoothed = osc_ctx[osc].pos_smoothed;
    }
    instance->volume_current = instance->volume;
    return 1U;
}

ITCM_TEXT uint8_t brick6_wave_runtime_render_instance(uint8_t instance_id,
                                                       float *out_mono,
                                                       uint32_t frames)
{
    const uint8_t capture_mask = synth_waveform_audio_instance_mask(instance_id);
    if (capture_mask == 0U)
    {
        return wave_render_instance_block(instance_id, out_mono, frames, 0U);
    }
    return wave_render_instance_block(instance_id, out_mono, frames, capture_mask);
}

const brick6_wave_runtime_voice_t *brick6_wave_runtime_get_voice(uint8_t instance_id)
{
    const brick6_wave_runtime_instance_t *const instance = wave_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}

const brick6_wave_runtime_osc_t *brick6_wave_runtime_get_osc(uint8_t instance_id,
                                                             uint8_t osc)
{
    const brick6_wave_runtime_instance_t *const instance = wave_get_instance(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return NULL;
    }
    return &instance->osc[osc];
}
