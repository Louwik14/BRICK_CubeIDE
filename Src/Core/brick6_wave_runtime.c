#include "Core/brick6_wave_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Sampler/wavetable_pool.h"
#include "Storage/memory_layout.h"

#define WAVE_DEFAULT_NOTE          60U
#define WAVE_SAMPLE_RATE           48000.0f
#define WAVE_A4_NOTE               69.0f
#define WAVE_A4_FREQ               440.0f
#define WAVE_POS_SMOOTH_COEFF      0.004f
#define WAVE_POS_SMOOTH_SNAP       0.000001f
#define WAVE_FRAME_SAMPLE_COUNT_F  ((float)WAVETABLE_FRAME_SAMPLE_COUNT)
#define WAVE_NOTE_ON_DECLICK_SAMPLES (32U)
#define WAVE_NOTE_ON_DECLICK_RECIP   (1.0f / (float)WAVE_NOTE_ON_DECLICK_SAMPLES)

typedef struct
{
    brick6_wave_runtime_osc_t osc[BRICK6_WAVE_OSC_COUNT];
    brick6_wave_runtime_voice_t voice;
} brick6_wave_runtime_instance_t;

typedef struct
{
    brick6_wave_runtime_osc_t *osc;
    const float *data;
    uint32_t frame_count;
    const float *frame0_data;
    const float *frame1_data;
    float max_frame;
    float frame_frac;
    float level;
    uint8_t reverse;
    uint8_t invert;
    uint8_t pos_stable;
    uint8_t frame_interp;
    uint8_t valid;
} wave_osc_block_ctx_t;

AUDIO_HOT static brick6_wave_runtime_instance_t g_wave_runtime[BRICK6_WAVE_MAX_INSTANCES];

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
    if (instance_id >= BRICK6_WAVE_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_wave_runtime[instance_id];
}

static const brick6_wave_runtime_instance_t *wave_get_instance(uint8_t instance_id)
{
    if (instance_id >= BRICK6_WAVE_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_wave_runtime[instance_id];
}

static void wave_resolve_table(brick6_wave_runtime_osc_t *osc)
{
    if (osc == NULL)
    {
        return;
    }

    uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    if (osc->table_global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
    {
        if (sample_global_pool_resolve_backend(osc->table_global_slot,
                                               SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               &wavetable_slot) != 0U)
        {
            const wavetable_slot_t *const table = wavetable_pool_get_slot(wavetable_slot);
            if ((table != NULL)
                    && (table->state == WAVETABLE_SLOT_READY)
                    && (table->data != NULL)
                    && (table->frame_count != 0U)
                    && (table->frame_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT))
            {
                osc->table_wavetable_slot = wavetable_slot;
                osc->table_generation = table->generation;
                return;
            }
        }

        osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        osc->table_generation = 0U;
        return;
    }

    if (osc->table_wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
    {
        const wavetable_slot_t *const table = wavetable_pool_get_slot(osc->table_wavetable_slot);
        if ((table == NULL)
                || (table->state != WAVETABLE_SLOT_READY)
                || (table->data == NULL)
                || (table->frame_count == 0U)
                || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
                || (table->generation != osc->table_generation))
        {
            osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
            osc->table_generation = 0U;
            return;
        }

        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        if (sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                               osc->table_wavetable_slot,
                                               &global_slot) != 0U)
        {
            osc->table_global_slot = global_slot;
        }
        return;
    }

    osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    osc->table_generation = 0U;
}

static float wave_note_to_phase_inc(float note, float tune_semitones)
{
    const float semitone_from_a4 = (note + tune_semitones) - WAVE_A4_NOTE;
    const float hz = WAVE_A4_FREQ * powf(2.0f, semitone_from_a4 * (1.0f / 12.0f));
    return ((float)WAVETABLE_FRAME_SAMPLE_COUNT * hz) * (1.0f / WAVE_SAMPLE_RATE);
}

static float wave_phase_start(uint8_t phase_mode)
{
    switch ((brick6_wave_phase_t)phase_mode)
    {
        case BRICK6_WAVE_PHASE_90:
            return (float)WAVETABLE_FRAME_SAMPLE_COUNT * 0.25f;
        case BRICK6_WAVE_PHASE_180:
            return (float)WAVETABLE_FRAME_SAMPLE_COUNT * 0.5f;
        case BRICK6_WAVE_PHASE_270:
            return (float)WAVETABLE_FRAME_SAMPLE_COUNT * 0.75f;
        case BRICK6_WAVE_PHASE_0:
        default:
            return 0.0f;
    }
}

static float wave_absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float wave_remap_pos_to_scan_zone(const brick6_wave_runtime_osc_t *osc)
{
    float start = wave_clampf(osc->start, 0.0f, 1.0f);
    float end = wave_clampf(osc->end, 0.0f, 1.0f);
    if (end < start)
    {
        const float tmp = start;
        start = end;
        end = tmp;
    }
    return start + ((end - start) * wave_clampf(osc->pos, 0.0f, 1.0f));
}

static float wave_smooth_pos(brick6_wave_runtime_osc_t *osc)
{
    const float target_pos = wave_remap_pos_to_scan_zone(osc);
    const float delta = target_pos - osc->pos_smoothed;
    if (wave_absf(delta) <= WAVE_POS_SMOOTH_SNAP)
    {
        osc->pos_smoothed = target_pos;
    }
    else
    {
        osc->pos_smoothed += delta * WAVE_POS_SMOOTH_COEFF;
    }
    return wave_clampf(osc->pos_smoothed, 0.0f, 1.0f);
}

static void wave_advance_pos_silent_block(brick6_wave_runtime_osc_t *osc, uint32_t frames)
{
    if ((osc == NULL) || (frames == 0U))
    {
        return;
    }

    const float target_pos = wave_remap_pos_to_scan_zone(osc);
    const float delta = target_pos - osc->pos_smoothed;
    if (wave_absf(delta) <= WAVE_POS_SMOOTH_SNAP)
    {
        osc->pos_smoothed = target_pos;
        return;
    }

    float remaining = 1.0f;
    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        remaining *= (1.0f - WAVE_POS_SMOOTH_COEFF);
    }
    osc->pos_smoothed += delta * (1.0f - remaining);
    if (wave_absf(target_pos - osc->pos_smoothed) <= WAVE_POS_SMOOTH_SNAP)
    {
        osc->pos_smoothed = target_pos;
    }
    osc->pos_smoothed = wave_clampf(osc->pos_smoothed, 0.0f, 1.0f);
}

static void wave_snap_pos(brick6_wave_runtime_osc_t *osc)
{
    if (osc != NULL)
    {
        osc->pos_smoothed = wave_clampf(wave_remap_pos_to_scan_zone(osc), 0.0f, 1.0f);
    }
}

static void wave_update_pitch(brick6_wave_runtime_instance_t *instance, uint8_t osc_index)
{
    if ((instance == NULL) || (osc_index >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    const float note = instance->voice.has_active_note != 0U
        ? (float)instance->voice.active_note
        : (float)WAVE_DEFAULT_NOTE;
    instance->osc[osc_index].phase_inc =
        wave_note_to_phase_inc(note, instance->osc[osc_index].tune_semitones);
}

static void wave_reset_osc(brick6_wave_runtime_osc_t *osc, uint8_t osc_index)
{
    memset(osc, 0, sizeof(*osc));
    osc->table_global_slot = 0U;
    osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
    osc->table_generation = 0U;
    osc->level = (osc_index == 0U) ? 1.0f : 0.0f;
    osc->start = 0.0f;
    osc->end = 1.0f;
    osc->pos = 0.0f;
    osc->pos_smoothed = 0.0f;
    osc->phase_mode = (uint8_t)BRICK6_WAVE_PHASE_0;
    osc->flip = (uint8_t)BRICK6_WAVE_FLIP_OFF;
    osc->phase = 0.0f;
    osc->phase_inc = wave_note_to_phase_inc((float)WAVE_DEFAULT_NOTE, 0.0f);
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
        wave_reset_osc(&instance->osc[osc], osc);
    }
}

static float wave_read_frame_sample(const float *frame_data,
                                    float phase,
                                    uint8_t reverse)
{
    float read_phase = phase;
    if (reverse != 0U)
    {
        read_phase = ((float)WAVETABLE_FRAME_SAMPLE_COUNT - 1.0f) - read_phase;
    }

    uint32_t i0 = (uint32_t)read_phase;
    if (i0 >= WAVETABLE_FRAME_SAMPLE_COUNT)
    {
        i0 = WAVETABLE_FRAME_SAMPLE_COUNT - 1U;
    }
    uint32_t i1 = i0 + 1U;
    if (i1 >= WAVETABLE_FRAME_SAMPLE_COUNT)
    {
        i1 = 0U;
    }

    const float frac = read_phase - (float)i0;
    const float a = frame_data[i0];
    const float b = frame_data[i1];
    return a + ((b - a) * frac);
}

static void wave_wrap_phase(brick6_wave_runtime_osc_t *osc)
{
    osc->phase += osc->phase_inc;
    if (osc->phase >= WAVE_FRAME_SAMPLE_COUNT_F)
    {
        osc->phase -= WAVE_FRAME_SAMPLE_COUNT_F;
        while (osc->phase >= WAVE_FRAME_SAMPLE_COUNT_F)
        {
            osc->phase -= WAVE_FRAME_SAMPLE_COUNT_F;
        }
    }
    else if (osc->phase < 0.0f)
    {
        osc->phase += WAVE_FRAME_SAMPLE_COUNT_F;
        while (osc->phase < 0.0f)
        {
            osc->phase += WAVE_FRAME_SAMPLE_COUNT_F;
        }
    }
}

static void wave_prepare_stable_frame(wave_osc_block_ctx_t *ctx, float smoothed_pos)
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

    ctx->frame_frac = frame_f - (float)frame0;
    ctx->frame0_data = &ctx->data[frame0 * WAVETABLE_FRAME_SAMPLE_COUNT];
    ctx->frame1_data = &ctx->data[frame1 * WAVETABLE_FRAME_SAMPLE_COUNT];
    ctx->frame_interp = ((frame1 != frame0) && (ctx->frame_frac != 0.0f)) ? 1U : 0U;
    ctx->pos_stable = 1U;
}

static uint8_t wave_prepare_osc_block(brick6_wave_runtime_osc_t *osc,
                                      wave_osc_block_ctx_t *ctx,
                                      uint32_t frames)
{
    memset(ctx, 0, sizeof(*ctx));
    if (osc == NULL)
    {
        return 0U;
    }

    ctx->osc = osc;
    ctx->level = osc->level;
    if (ctx->level <= 0.0f)
    {
        wave_advance_pos_silent_block(osc, frames);
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

    const wavetable_slot_t *const table = wavetable_pool_get_slot(osc->table_wavetable_slot);
    if ((table == NULL)
        || (table->state != WAVETABLE_SLOT_READY)
        || (table->data == NULL)
        || (table->frame_count == 0U)
        || (table->frame_sample_count != WAVETABLE_FRAME_SAMPLE_COUNT)
        || (table->generation != osc->table_generation))
    {
        osc->table_wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        osc->table_generation = 0U;
        wave_advance_pos_silent_block(osc, frames);
        return 0U;
    }

    ctx->data = table->data;
    ctx->frame_count = table->frame_count;
    ctx->max_frame = (table->frame_count > 1U) ? (float)(table->frame_count - 1U) : 0.0f;
    ctx->reverse = ((osc->flip == (uint8_t)BRICK6_WAVE_FLIP_Y)
                    || (osc->flip == (uint8_t)BRICK6_WAVE_FLIP_XY)) ? 1U : 0U;
    ctx->invert = ((osc->flip == (uint8_t)BRICK6_WAVE_FLIP_X)
                   || (osc->flip == (uint8_t)BRICK6_WAVE_FLIP_XY)) ? 1U : 0U;
    const float target_pos = wave_remap_pos_to_scan_zone(osc);
    if (wave_absf(target_pos - osc->pos_smoothed) <= WAVE_POS_SMOOTH_SNAP)
    {
        osc->pos_smoothed = wave_clampf(target_pos, 0.0f, 1.0f);
        wave_prepare_stable_frame(ctx, osc->pos_smoothed);
    }
    ctx->valid = 1U;
    return 1U;
}

static float wave_render_osc_sample_dynamic_ctx(const wave_osc_block_ctx_t *ctx)
{
    brick6_wave_runtime_osc_t *const osc = ctx->osc;
    const float smoothed_pos = wave_smooth_pos(osc);
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

    const float frame_frac = frame_f - (float)frame0;
    const float *const frame0_data = &ctx->data[frame0 * WAVETABLE_FRAME_SAMPLE_COUNT];
    float out = wave_read_frame_sample(frame0_data, osc->phase, ctx->reverse);
    if ((frame1 != frame0) && (frame_frac != 0.0f))
    {
        const float *const frame1_data = &ctx->data[frame1 * WAVETABLE_FRAME_SAMPLE_COUNT];
        const float b = wave_read_frame_sample(frame1_data, osc->phase, ctx->reverse);
        out += (b - out) * frame_frac;
    }

    if (ctx->invert != 0U)
    {
        out = -out;
    }
    wave_wrap_phase(osc);
    return out * ctx->level;
}

static float wave_render_osc_sample_stable_ctx(const wave_osc_block_ctx_t *ctx)
{
    brick6_wave_runtime_osc_t *const osc = ctx->osc;
    float out = wave_read_frame_sample(ctx->frame0_data, osc->phase, ctx->reverse);
    if (ctx->frame_interp != 0U)
    {
        const float b = wave_read_frame_sample(ctx->frame1_data, osc->phase, ctx->reverse);
        out += (b - out) * ctx->frame_frac;
    }

    if (ctx->invert != 0U)
    {
        out = -out;
    }
    wave_wrap_phase(osc);
    return out * ctx->level;
}

static float wave_apply_note_on_declick(brick6_wave_runtime_voice_t *voice, float sample)
{
    if (voice->declick_remaining == 0U)
    {
        voice->last_output = sample;
        return sample;
    }

    const float fade = (float)(WAVE_NOTE_ON_DECLICK_SAMPLES - voice->declick_remaining)
        * WAVE_NOTE_ON_DECLICK_RECIP;
    const float out = voice->declick_start + ((sample - voice->declick_start) * fade);
    voice->declick_remaining--;
    voice->last_output = out;
    return out;
}

void brick6_wave_runtime_init(void)
{
    for (uint8_t instance = 0U; instance < BRICK6_WAVE_MAX_INSTANCES; ++instance)
    {
        wave_reset_instance(&g_wave_runtime[instance]);
    }
}

void brick6_wave_runtime_reset_instance(uint8_t instance_id)
{
    wave_reset_instance(wave_get_instance_mut(instance_id));
}

void brick6_wave_runtime_set_osc_table_global(uint8_t instance_id, uint8_t osc, uint16_t global_slot)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    brick6_wave_runtime_osc_t *const target = &instance->osc[osc];
    const uint16_t previous_slot = target->table_wavetable_slot;
    const uint32_t previous_generation = target->table_generation;
    target->table_global_slot = global_slot;
    wave_resolve_table(target);
    if ((target->table_wavetable_slot < WAVETABLE_POOL_MAX_SLOTS)
            && ((target->table_wavetable_slot != previous_slot)
                || (target->table_generation != previous_generation)))
    {
        wave_snap_pos(target);
    }
}

void brick6_wave_runtime_set_osc_table_wavetable(uint8_t instance_id, uint8_t osc, uint16_t wavetable_slot)
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
    const wavetable_slot_t *const table = wavetable_pool_get_slot(wavetable_slot);
    if ((table != NULL)
            && (table->state == WAVETABLE_SLOT_READY)
            && (table->data != NULL)
            && (table->frame_count != 0U)
            && (table->frame_sample_count == WAVETABLE_FRAME_SAMPLE_COUNT))
    {
        target->table_wavetable_slot = wavetable_slot;
        target->table_generation = table->generation;
        (void)sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_WAVETABLE,
                                                 wavetable_slot,
                                                 &target->table_global_slot);
        if ((target->table_wavetable_slot != previous_slot)
                || (target->table_generation != previous_generation))
        {
            wave_snap_pos(target);
        }
    }
}

void brick6_wave_runtime_set_osc_level(uint8_t instance_id, uint8_t osc, float level)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].level = wave_clampf(level, 0.0f, 1.0f);
}

void brick6_wave_runtime_set_osc_tune(uint8_t instance_id, uint8_t osc, float semitones)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].tune_semitones = wave_clampf(semitones, -60.0f, 60.0f);
    wave_update_pitch(instance, osc);
}

void brick6_wave_runtime_set_osc_pos(uint8_t instance_id, uint8_t osc, float pos)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].pos = wave_clampf(pos, 0.0f, 1.0f);
}

void brick6_wave_runtime_set_osc_start(uint8_t instance_id, uint8_t osc, float start)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].start = wave_clampf(start, 0.0f, 1.0f);
}

void brick6_wave_runtime_set_osc_end(uint8_t instance_id, uint8_t osc, float end)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].end = wave_clampf(end, 0.0f, 1.0f);
}

void brick6_wave_runtime_set_osc_phase(uint8_t instance_id, uint8_t osc, brick6_wave_phase_t phase)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].phase_mode =
        ((uint8_t)phase < (uint8_t)BRICK6_WAVE_PHASE_COUNT) ? (uint8_t)phase : 0U;
}

void brick6_wave_runtime_set_osc_flip(uint8_t instance_id, uint8_t osc, brick6_wave_flip_t flip)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return;
    }
    instance->osc[osc].flip =
        ((uint8_t)flip < (uint8_t)BRICK6_WAVE_FLIP_COUNT) ? (uint8_t)flip : 0U;
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
    instance->voice.declick_start = instance->voice.last_output;
    instance->voice.declick_remaining =
        (instance->voice.velocity > 0.0f) ? WAVE_NOTE_ON_DECLICK_SAMPLES : 0U;
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        instance->osc[osc].phase = wave_phase_start(instance->osc[osc].phase_mode);
        wave_snap_pos(&instance->osc[osc]);
        wave_update_pitch(instance, osc);
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
    instance->voice.declick_remaining = 0U;
    instance->voice.declick_start = 0.0f;
    instance->voice.last_output = 0.0f;
}

void brick6_wave_runtime_clear_trigger(uint8_t instance_id)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->voice.trigger = 0U;
    }
}

uint8_t brick6_wave_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if ((instance == NULL) || (out_mono == NULL) || (frames == 0U))
    {
        return 0U;
    }

    if (instance->voice.velocity <= 0.0f)
    {
        return 0U;
    }

    wave_osc_block_ctx_t osc_ctx[BRICK6_WAVE_OSC_COUNT];
    uint8_t active_osc_count = 0U;
    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        if (wave_prepare_osc_block(&instance->osc[osc], &osc_ctx[active_osc_count], frames) != 0U)
        {
            active_osc_count++;
        }
    }
    if (active_osc_count == 0U)
    {
        instance->voice.last_output = 0.0f;
        instance->voice.declick_remaining = 0U;
        return 0U;
    }

    memset(out_mono, 0, frames * sizeof(float));
    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        float mono = 0.0f;
        for (uint8_t osc = 0U; osc < active_osc_count; ++osc)
        {
            mono += (osc_ctx[osc].pos_stable != 0U)
                ? wave_render_osc_sample_stable_ctx(&osc_ctx[osc])
                : wave_render_osc_sample_dynamic_ctx(&osc_ctx[osc]);
        }
        out_mono[frame] =
            wave_apply_note_on_declick(&instance->voice, mono * instance->voice.velocity);
    }
    return 1U;
}

const brick6_wave_runtime_voice_t *brick6_wave_runtime_get_voice(uint8_t instance_id)
{
    const brick6_wave_runtime_instance_t *const instance = wave_get_instance(instance_id);
    return (instance != NULL) ? &instance->voice : NULL;
}

const brick6_wave_runtime_osc_t *brick6_wave_runtime_get_osc(uint8_t instance_id, uint8_t osc)
{
    const brick6_wave_runtime_instance_t *const instance = wave_get_instance(instance_id);
    if ((instance == NULL) || (osc >= BRICK6_WAVE_OSC_COUNT))
    {
        return NULL;
    }
    return &instance->osc[osc];
}
