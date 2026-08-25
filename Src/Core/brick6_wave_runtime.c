#include "Core/brick6_wave_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Audio/synth_waveform_snapshot.h"
#include "Audio/audio_shared_memory.h"
#include "Audio/audio_wavetable_registry.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/memory_layout.h"

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
#define WAVE_S16_TO_FLOAT          (1.0f / 32768.0f)
#define WAVE_GAIN_SILENCE_EPS      0.00001f
#define BRICK6_WAVE_OUTPUT_GAIN    0.42169650f
#if defined(BRICK6_VARIANT_LOWCOST)
#define WAVE_OUTPUT_TRIM           0.30f
#else
#define WAVE_OUTPUT_TRIM           1.0f
#endif

typedef struct wave_osc_block_ctx_t wave_osc_block_ctx_t;
enum
{
    WAVE_CONT_POS_BASE = 0,
    WAVE_CONT_VOLUME = WAVE_CONT_POS_BASE + BRICK6_WAVE_OSC_COUNT,
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
    const int16_t *data;
    const int16_t *frame0_data;
    const int16_t *frame1_data;
    uint32_t frame_count;
    uint32_t cycle_sample_count;
    uint32_t cycle_stride;
    uint32_t phase_shift;
    uint32_t phase_mask;
    float phase_to_float;
    float max_frame;
    float frame_frac;
    float balance_gain_current;
    float balance_gain_step;
    float pos_smoothed;
    double phase_inc_current;
    double phase_inc_step;
    uint32_t phase;
    uint32_t phase_inc_value;
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

static void wave_resolve_table(brick6_wave_runtime_osc_t *osc)
{
    if (osc == NULL)
    {
        return;
    }

    audio_wavetable_descriptor_t table;
    if (osc->table_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        if (audio_wavetable_registry_resolve_global(osc->table_global_slot,
                                                    &table) != 0U)
        {
            osc->table_wavetable_slot = table.wavetable_slot;
            osc->table_generation = table.generation;
            return;
        }

        osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        osc->table_generation = 0U;
        return;
    }

    if (osc->table_wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        if (audio_wavetable_registry_resolve(osc->table_wavetable_slot,
                                             osc->table_generation, &table) == 0U)
        {
            osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
            osc->table_generation = 0U;
            return;
        }

        osc->table_global_slot = table.global_slot;
        return;
    }

    osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    osc->table_generation = 0U;
}

static uint32_t wave_note_to_phase_inc(float note, float tune_semitones)
{
    const float semitone_from_a4 = (note + tune_semitones) - WAVE_A4_NOTE;
    const float hz = WAVE_A4_FREQ * powf(2.0f, semitone_from_a4 * (1.0f / 12.0f));
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
float wave_read_frame_sample(const int16_t *frame_data,
                             uint32_t phase,
                             uint32_t cycle_sample_count,
                             uint32_t phase_shift,
                             uint32_t phase_mask,
                             float phase_to_float)
{
    uint32_t i0 = phase >> phase_shift;
    uint32_t frac_q = phase & phase_mask;

    const uint32_t i1 = (i0 + 1U) & (cycle_sample_count - 1U);
    const float frac = (float)frac_q * phase_to_float;
    const float a = (float)frame_data[i0];
    const float b = (float)frame_data[i1];
    return (a + ((b - a) * frac)) * WAVE_S16_TO_FLOAT;
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

static void wave_select_frame_from_pos(wave_osc_block_ctx_t *ctx, float smoothed_pos)
{
    const float frame_f = smoothed_pos * ctx->max_frame;
    uint32_t frame0 = (uint32_t)frame_f;
    if (frame0 >= ctx->frame_count)
    {
        frame0 = ctx->frame_count - 1U;
    }
    uint32_t frame1 = frame0 + 1U;
    if (frame1 >= ctx->frame_count)
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
    ctx->frame0_data = &ctx->data[frame0 * ctx->cycle_stride];
    ctx->frame1_data = &ctx->data[frame1 * ctx->cycle_stride];
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

    const uint16_t previous_slot = osc->table_wavetable_slot;
    const uint32_t previous_generation = osc->table_generation;
    wave_resolve_table(osc);
    if ((osc->table_wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
            && ((osc->table_wavetable_slot != previous_slot)
                || (osc->table_generation != previous_generation)))
    {
        wave_snap_pos(osc);
    }

    audio_wavetable_descriptor_t table;
    if (audio_wavetable_registry_resolve(osc->table_wavetable_slot,
                                         osc->table_generation, &table) == 0U)
    {
        osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        osc->table_generation = 0U;
        wave_advance_pos_silent_block(osc, frames);
        return 0U;
    }

    const audio_wavetable_descriptor_t *const mipmap = &table;
    const audio_wavetable_band_t *band = NULL;
    const uint32_t phase_inc = osc->phase_inc;
    if ((mipmap != NULL) && (mipmap->band_count != 0U))
    {
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
        band = &mipmap->bands[selected];
    }
    ctx->data = (const int16_t *)audio_shared_memory_resolve(
        (band != NULL) ? &band->data : &table.base_data);
    if (ctx->data == NULL) return 0U;
    ctx->frame_count = table.frame_count;
    ctx->cycle_sample_count = (band != NULL) ? band->cycle_sample_count
                                             : WAVETABLE_FRAME_SAMPLE_COUNT;
    ctx->cycle_stride = ctx->cycle_sample_count
        + ((band != NULL) ? mipmap->duplicate_sample_count : 0U);
    ctx->phase_shift = 32U - ((band != NULL) ? band->cycle_magnitude : WAVE_PHASE_INDEX_BITS);
    ctx->phase_mask = (1UL << ctx->phase_shift) - 1UL;
    ctx->phase_to_float = 1.0f / (float)(1UL << ctx->phase_shift);
    ctx->max_frame = (table.frame_count > 1U) ? (float)(table.frame_count - 1U) : 0.0f;

    const float target_pos = wave_clampf(osc->pos, 0.0f, 1.0f);
    ctx->pos_smoothed = target_pos;
    osc->pos_smoothed = target_pos;
    wave_select_frame_from_pos(ctx, target_pos);
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
    target->table_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    audio_wavetable_descriptor_t table;
    if (audio_wavetable_registry_resolve(wavetable_slot, generation, &table) != 0U)
    {
        target->table_wavetable_slot = wavetable_slot;
        target->table_generation = table.generation;
        target->table_global_slot = table.global_slot;
        if ((target->table_wavetable_slot != previous_slot)
                || (target->table_generation != previous_generation))
        {
            wave_snap_pos(target);
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
                osc->table_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
                osc->table_generation = 0U;
                osc->mipmap_band = 0U;
                osc->mipmap_phase_inc = 0U;
                wave_touch_config(instance);
            }
        }
    }
    audio_wavetable_registry_remove(wavetable_slot, generation);
    wavetable_pool_audio_ack_retire(wavetable_slot, generation);
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
            const float raw = wave_render_osc_sample_stable_ctx(
                &osc_ctx[osc], carrier_phase);
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
