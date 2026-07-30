#include "Core/brick6_wave_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Sampler/wavetable_pool.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

#define WAVE_DEFAULT_NOTE          60U
#define WAVE_SAMPLE_RATE           48000.0f
#define WAVE_A4_NOTE               69.0f
#define WAVE_A4_FREQ               440.0f
#define WAVE_POS_SMOOTH_COEFF      0.004f
#define WAVE_POS_SMOOTH_SNAP       0.000001f
#define WAVE_FRAME_FRAC_EPS        0.000001f
#define WAVE_DEFAULT_FRAME_INTERP  0U
#define WAVE_DEFAULT_SAMPLE_INTERP 1U
#define WAVE_DEFAULT_POS_UPDATE    BRICK6_WAVE_POS_UPDATE_16
#define WAVE_DEFAULT_POS_SMOOTH    1U
#define WAVE_PHASE_INDEX_BITS      11U
#define WAVE_PHASE_FRAC_BITS       (32U - WAVE_PHASE_INDEX_BITS)
#define WAVE_PHASE_FRAC_MASK       ((1UL << WAVE_PHASE_FRAC_BITS) - 1UL)
#define WAVE_PHASE_FRAC_TO_FLOAT   (1.0f / (float)(1UL << WAVE_PHASE_FRAC_BITS))
#define WAVE_PHASE_SAMPLE_ROUND    (1UL << (WAVE_PHASE_FRAC_BITS - 1U))
#define WAVE_PHASE_SCALE           4294967296.0
#define WAVE_PHASE_90              0x40000000UL
#define WAVE_PHASE_180             0x80000000UL
#define WAVE_PHASE_270             0xC0000000UL
#define WAVE_S16_TO_FLOAT          (1.0f / 32768.0f)
#define WAVE_LEVEL_SILENCE_EPS     0.00001f
#if defined(BRICK6_VARIANT_LOWCOST)
#define WAVE_OUTPUT_TRIM           0.30f
#else
#define WAVE_OUTPUT_TRIM           1.0f
#endif

typedef struct
{
    brick6_wave_runtime_osc_t osc[BRICK6_WAVE_OSC_COUNT];
    brick6_wave_runtime_voice_t voice;
    brick6_wave_runtime_quality_t quality;
} brick6_wave_runtime_instance_t;

typedef struct
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
    float level_current;
    float level_step;
    double phase_inc_current;
    double phase_inc_step;
    uint8_t reverse;
    uint8_t invert;
    uint8_t pos_stable;
    uint8_t frame_interp;
    uint8_t frame_interp_enabled;
    uint8_t sample_interp_enabled;
    uint8_t pos_smooth_enabled;
    uint8_t pos_update_samples;
    uint8_t pos_chunk_remaining;
} wave_osc_block_ctx_t;

AUDIO_HOT static brick6_wave_runtime_instance_t g_wave_runtime[BRICK6_WAVE_MAX_INSTANCES];
static volatile uint8_t g_wave_dwt_enabled;
static brick6_wave_runtime_dwt_stats_t g_wave_dwt_stats;

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

static uint32_t wave_phase_start(uint8_t phase_mode)
{
    switch ((brick6_wave_phase_t)phase_mode)
    {
        case BRICK6_WAVE_PHASE_90:
            return WAVE_PHASE_90;
        case BRICK6_WAVE_PHASE_180:
            return WAVE_PHASE_180;
        case BRICK6_WAVE_PHASE_270:
            return WAVE_PHASE_270;
        case BRICK6_WAVE_PHASE_0:
        default:
            return 0U;
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

static void wave_advance_pos_silent_block(brick6_wave_runtime_osc_t *osc,
                                          uint32_t frames,
                                          uint8_t smooth_enabled)
{
    if ((osc == NULL) || (frames == 0U))
    {
        return;
    }

    const float target_pos = wave_remap_pos_to_scan_zone(osc);
    if (smooth_enabled == 0U)
    {
        osc->pos_smoothed = wave_clampf(target_pos, 0.0f, 1.0f);
        return;
    }

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

static uint8_t wave_pos_update_to_samples(brick6_wave_pos_update_t update)
{
    switch (update)
    {
        case BRICK6_WAVE_POS_UPDATE_8:
            return 8U;
        case BRICK6_WAVE_POS_UPDATE_32:
            return 32U;
        case BRICK6_WAVE_POS_UPDATE_16:
            return 16U;
        case BRICK6_WAVE_POS_UPDATE_FULL:
        default:
            return 1U;
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
    osc->level_current = osc->level;
    osc->start = 0.0f;
    osc->end = 1.0f;
    osc->pos = 0.0f;
    osc->pos_smoothed = 0.0f;
    osc->phase_mode = (uint8_t)BRICK6_WAVE_PHASE_0;
    osc->flip = (uint8_t)BRICK6_WAVE_FLIP_OFF;
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
        wave_reset_osc(&instance->osc[osc], osc);
    }
    instance->quality.frame_interp_enabled = WAVE_DEFAULT_FRAME_INTERP;
    instance->quality.sample_interp_enabled = WAVE_DEFAULT_SAMPLE_INTERP;
    instance->quality.pos_update = WAVE_DEFAULT_POS_UPDATE;
    instance->quality.pos_smooth_enabled = WAVE_DEFAULT_POS_SMOOTH;
}

static float wave_read_frame_sample(const int16_t *frame_data,
                                    uint32_t phase,
                                    uint32_t cycle_sample_count,
                                    uint32_t phase_shift,
                                    uint32_t phase_mask,
                                    float phase_to_float,
                                    uint8_t reverse,
                                    uint8_t sample_interp_enabled)
{
    if (sample_interp_enabled == 0U)
    {
        uint32_t i0 = (phase + (1UL << (phase_shift - 1U))) >> phase_shift;
        i0 &= (cycle_sample_count - 1U);
        if (reverse != 0U)
        {
            i0 = (cycle_sample_count - 1U - i0) & (cycle_sample_count - 1U);
        }

        return (float)frame_data[i0] * WAVE_S16_TO_FLOAT;
    }

    uint32_t i0 = phase >> phase_shift;
    uint32_t frac_q = phase & phase_mask;
    if (reverse != 0U)
    {
        if (frac_q != 0U)
        {
            i0 = (cycle_sample_count - 2U - i0) & (cycle_sample_count - 1U);
            frac_q = (phase_mask + 1U) - frac_q;
        }
        else
        {
            i0 = (cycle_sample_count - 1U - i0) & (cycle_sample_count - 1U);
        }
    }

    const uint32_t i1 = (i0 + 1U) & (cycle_sample_count - 1U);
    const float frac = (float)frac_q * phase_to_float;
    const float a = (float)frame_data[i0];
    const float b = (float)frame_data[i1];
    return (a + ((b - a) * frac)) * WAVE_S16_TO_FLOAT;
}

static void wave_advance_phase(wave_osc_block_ctx_t *ctx)
{
    ctx->phase_inc_current += ctx->phase_inc_step;
    ctx->osc->phase_inc_current = (uint32_t)(ctx->phase_inc_current + 0.5);
    ctx->osc->phase += ctx->osc->phase_inc_current;
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
    if (ctx->frame_interp_enabled == 0U)
    {
        if ((frame_frac >= 0.5f) && (frame1 != frame0))
        {
            frame0 = frame1;
        }
        frame_frac = 0.0f;
    }
    else if (((1.0f - frame_frac) <= WAVE_FRAME_FRAC_EPS) && (frame1 != frame0))
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

static void wave_prepare_stable_frame(wave_osc_block_ctx_t *ctx, float smoothed_pos)
{
    wave_select_frame_from_pos(ctx, smoothed_pos);
    ctx->pos_stable = 1U;
}

static uint8_t wave_prepare_osc_block(brick6_wave_runtime_osc_t *osc,
                                      wave_osc_block_ctx_t *ctx,
                                      const brick6_wave_runtime_quality_t *quality,
                                      uint32_t frames)
{
    memset(ctx, 0, sizeof(*ctx));
    if (osc == NULL)
    {
        return 0U;
    }

    ctx->osc = osc;
    ctx->frame_interp_enabled = (quality != NULL) ? quality->frame_interp_enabled : WAVE_DEFAULT_FRAME_INTERP;
    ctx->sample_interp_enabled = (quality != NULL) ? quality->sample_interp_enabled : WAVE_DEFAULT_SAMPLE_INTERP;
    ctx->pos_smooth_enabled = (quality != NULL) ? quality->pos_smooth_enabled : WAVE_DEFAULT_POS_SMOOTH;
    ctx->pos_update_samples = wave_pos_update_to_samples((quality != NULL) ? quality->pos_update : WAVE_DEFAULT_POS_UPDATE);
    ctx->level_current = osc->level_current;
    ctx->level_step = (frames != 0U)
        ? ((osc->level - osc->level_current) / (float)frames) : 0.0f;
    ctx->phase_inc_current = (double)osc->phase_inc_current;
    ctx->phase_inc_step = (frames != 0U)
        ? (((double)osc->phase_inc - (double)osc->phase_inc_current) / (double)frames)
        : 0.0;
    if ((osc->level <= WAVE_LEVEL_SILENCE_EPS)
        && (osc->level_current <= WAVE_LEVEL_SILENCE_EPS))
    {
        wave_advance_pos_silent_block(osc, frames, ctx->pos_smooth_enabled);
        osc->level_current = osc->level;
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
        wave_advance_pos_silent_block(osc, frames, ctx->pos_smooth_enabled);
        return 0U;
    }

    const wavetable_mipmap_view_t *const mipmap = wavetable_pool_get_mipmap_view(
        osc->table_wavetable_slot);
    const wavetable_mipmap_band_t *band = NULL;
    if ((mipmap != NULL) && (mipmap->band_count != 0U))
    {
        uint8_t selected = osc->mipmap_band;
        if ((osc->mipmap_phase_inc != osc->phase_inc)
            || (selected >= mipmap->band_count))
        {
            selected = (uint8_t)(mipmap->band_count - 1U);
            for (uint8_t i = 0U; i < mipmap->band_count; ++i)
            {
                if (osc->phase_inc <= mipmap->bands[i].max_phase_increment)
                {
                    selected = i;
                    break;
                }
            }
            osc->mipmap_band = selected;
            osc->mipmap_phase_inc = osc->phase_inc;
        }
        band = &mipmap->bands[selected];
    }
    ctx->data = (band != NULL) ? band->data : table->data;
    ctx->frame_count = table->frame_count;
    ctx->cycle_sample_count = (band != NULL) ? band->cycle_sample_count
                                             : WAVETABLE_FRAME_SAMPLE_COUNT;
    ctx->cycle_stride = ctx->cycle_sample_count
        + ((band != NULL) ? mipmap->duplicate_sample_count : 0U);
    ctx->phase_shift = 32U - ((band != NULL) ? band->cycle_magnitude : WAVE_PHASE_INDEX_BITS);
    ctx->phase_mask = (1UL << ctx->phase_shift) - 1UL;
    ctx->phase_to_float = 1.0f / (float)(1UL << ctx->phase_shift);
    ctx->max_frame = (table->frame_count > 1U) ? (float)(table->frame_count - 1U) : 0.0f;
    ctx->reverse = ((osc->flip == (uint8_t)BRICK6_WAVE_FLIP_Y)
                    || (osc->flip == (uint8_t)BRICK6_WAVE_FLIP_XY)) ? 1U : 0U;
    ctx->invert = ((osc->flip == (uint8_t)BRICK6_WAVE_FLIP_X)
                   || (osc->flip == (uint8_t)BRICK6_WAVE_FLIP_XY)) ? 1U : 0U;

    const float target_pos = wave_remap_pos_to_scan_zone(osc);
    if ((ctx->pos_smooth_enabled == 0U) || (wave_absf(target_pos - osc->pos_smoothed) <= WAVE_POS_SMOOTH_SNAP))
    {
        osc->pos_smoothed = wave_clampf(target_pos, 0.0f, 1.0f);
        wave_prepare_stable_frame(ctx, osc->pos_smoothed);
    }
    return 1U;
}

static float wave_render_osc_sample_dynamic_ctx(wave_osc_block_ctx_t *ctx)
{
    brick6_wave_runtime_osc_t *const osc = ctx->osc;
    float smoothed_pos = osc->pos_smoothed;
    if (ctx->pos_smooth_enabled != 0U)
    {
        smoothed_pos = wave_smooth_pos(osc);
    }
    else
    {
        smoothed_pos = wave_clampf(wave_remap_pos_to_scan_zone(osc), 0.0f, 1.0f);
        osc->pos_smoothed = smoothed_pos;
    }

    if (ctx->pos_chunk_remaining == 0U)
    {
        wave_select_frame_from_pos(ctx, smoothed_pos);
        ctx->pos_chunk_remaining = ctx->pos_update_samples;
    }
    ctx->pos_chunk_remaining--;

    float out = wave_read_frame_sample(ctx->frame0_data,
                                       osc->phase,
                                       ctx->cycle_sample_count,
                                       ctx->phase_shift,
                                       ctx->phase_mask,
                                       ctx->phase_to_float,
                                       ctx->reverse,
                                       ctx->sample_interp_enabled);
    if (ctx->frame_interp != 0U)
    {
        const float b = wave_read_frame_sample(ctx->frame1_data,
                                               osc->phase,
                                               ctx->cycle_sample_count,
                                               ctx->phase_shift,
                                               ctx->phase_mask,
                                               ctx->phase_to_float,
                                               ctx->reverse,
                                               ctx->sample_interp_enabled);
        out += (b - out) * ctx->frame_frac;
    }

    if (ctx->invert != 0U)
    {
        out = -out;
    }
    wave_advance_phase(ctx);
    ctx->level_current += ctx->level_step;
    osc->level_current = ctx->level_current;
    return out * ctx->level_current;
}

static float wave_render_osc_sample_stable_ctx(wave_osc_block_ctx_t *ctx)
{
    brick6_wave_runtime_osc_t *const osc = ctx->osc;
    float out = wave_read_frame_sample(ctx->frame0_data,
                                       osc->phase,
                                       ctx->cycle_sample_count,
                                       ctx->phase_shift,
                                       ctx->phase_mask,
                                       ctx->phase_to_float,
                                       ctx->reverse,
                                       ctx->sample_interp_enabled);
    if (ctx->frame_interp != 0U)
    {
        const float b = wave_read_frame_sample(ctx->frame1_data,
                                               osc->phase,
                                               ctx->cycle_sample_count,
                                               ctx->phase_shift,
                                               ctx->phase_mask,
                                               ctx->phase_to_float,
                                               ctx->reverse,
                                               ctx->sample_interp_enabled);
        out += (b - out) * ctx->frame_frac;
    }

    if (ctx->invert != 0U)
    {
        out = -out;
    }
    wave_advance_phase(ctx);
    ctx->level_current += ctx->level_step;
    osc->level_current = ctx->level_current;
    return out * ctx->level_current;
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

void brick6_wave_runtime_set_frame_interp(uint8_t instance_id, uint8_t enabled)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->quality.frame_interp_enabled = (enabled != 0U) ? 1U : 0U;
    }
}

void brick6_wave_runtime_set_sample_interp(uint8_t instance_id, uint8_t enabled)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->quality.sample_interp_enabled = (enabled != 0U) ? 1U : 0U;
    }
}

void brick6_wave_runtime_set_pos_update(uint8_t instance_id, brick6_wave_pos_update_t update)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->quality.pos_update =
            ((uint8_t)update < (uint8_t)BRICK6_WAVE_POS_UPDATE_COUNT)
                ? update
                : WAVE_DEFAULT_POS_UPDATE;
    }
}

void brick6_wave_runtime_set_pos_smooth(uint8_t instance_id, uint8_t enabled)
{
    brick6_wave_runtime_instance_t *const instance = wave_get_instance_mut(instance_id);
    if (instance != NULL)
    {
        instance->quality.pos_smooth_enabled = (enabled != 0U) ? 1U : 0U;
    }
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
        instance->osc[osc].phase = wave_phase_start(instance->osc[osc].phase_mode);
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
        if (wave_prepare_osc_block(&instance->osc[osc],
                                   &osc_ctx[active_osc_count],
                                   &instance->quality,
                                   frames) != 0U)
        {
            active_osc_count++;
        }
    }
    if (active_osc_count == 0U)
    {
        return 0U;
    }

    const uint32_t dwt_start = (g_wave_dwt_enabled != 0U) ? DWT->CYCCNT : 0U;
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
        out_mono[frame] = mono * instance->voice.velocity * WAVE_OUTPUT_TRIM;
    }
    if (g_wave_dwt_enabled != 0U)
    {
        const uint8_t bucket = (active_osc_count > 1U) ? 1U : 0U;
        const uint32_t cycles = DWT->CYCCNT - dwt_start;
        g_wave_dwt_stats.cycles[bucket] += cycles;
        g_wave_dwt_stats.blocks[bucket]++;
        if (cycles > g_wave_dwt_stats.max_cycles[bucket])
        {
            g_wave_dwt_stats.max_cycles[bucket] = cycles;
        }
    }
    return 1U;
}

void brick6_wave_runtime_dwt_enable(uint8_t enabled)
{
    if (enabled != 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        g_wave_dwt_enabled = 1U;
    }
    else
    {
        g_wave_dwt_enabled = 0U;
    }
}

void brick6_wave_runtime_dwt_reset(void)
{
    memset(&g_wave_dwt_stats, 0, sizeof(g_wave_dwt_stats));
}

void brick6_wave_runtime_dwt_read(brick6_wave_runtime_dwt_stats_t *out)
{
    if (out != NULL)
    {
        *out = g_wave_dwt_stats;
    }
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

const brick6_wave_runtime_quality_t *brick6_wave_runtime_get_quality(uint8_t instance_id)
{
    const brick6_wave_runtime_instance_t *const instance = wave_get_instance(instance_id);
    return (instance != NULL) ? &instance->quality : NULL;
}
