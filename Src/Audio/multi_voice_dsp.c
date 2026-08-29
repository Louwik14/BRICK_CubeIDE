/**
 * @file multi_voice_dsp.c
 * @brief Static storage and ownership helpers for Multi voice DSP state.
 */

#include "Audio/multi_voice_dsp.h"

#include <stddef.h>
#include <string.h>

#include "Platform/memory_layout.h"

AUDIO_WARM ALIGN32 static multi_voice_dsp_slot_t
    g_multi_voice_dsp_pool[MULTI_VOICE_DSP_SLOT_COUNT];

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(g_multi_voice_dsp_pool)
                   == (MULTI_VOICE_DSP_SLOT_COUNT * MULTI_VOICE_DSP_SLOT_SIZE_BYTES),
               "Multi DSP pool size must remain eight measured slots");
_Static_assert(__alignof__(g_multi_voice_dsp_pool) >= 32U,
               "Multi DSP pool must remain 32-byte aligned");
#endif

static uint8_t multi_voice_dsp_handle_is_valid(brick6_sampler_multi_voice_handle_t handle)
{
    return ((handle.voice_index < MULTI_VOICE_DSP_SLOT_COUNT)
            && (handle.generation != 0U))
               ? 1U
               : 0U;
}

static void multi_voice_dsp_init_filter(multi_voice_dsp_slot_t *slot,
                                         multi_voice_dsp_format_t format,
                                         float sample_rate)
{
    if (slot == NULL)
    {
        return;
    }

    slot->format = (uint8_t)format;
    if (format == MULTI_VOICE_DSP_FORMAT_STEREO)
    {
        fx_biquad_filter_init(&slot->filter.stereo.biquad, sample_rate);
    }
    else
    {
        slot->format = (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO;
        fx_biquad_filter_mono_init(&slot->filter.mono.biquad, sample_rate);
    }
}

static void multi_voice_dsp_reset_slot(multi_voice_dsp_slot_t *slot)
{
    if (slot == NULL)
    {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->owner_voice_index = BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID;
    slot->state = (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE;
    slot->format = (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO;
    slot->morph = 0.0f;
    slot->morph_target = 0.0f;
    slot->filter_mode = 0U;
    slot->sample_rate = MULTI_VOICE_DSP_DEFAULT_SAMPLE_RATE;
    slot->cutoff_hz = 16000.0f;
    slot->cutoff_target_hz = 16000.0f;
    slot->cutoff_mod_hz = 16000.0f;
    slot->cutoff_mod_target_hz = 16000.0f;
    slot->keytrack_ratio = 1.0f;
    slot->keytrack_ratio_target = 1.0f;
    slot->filter_retrigger_hard = 1U;
    slot->vca_retrigger_hard = 1U;
    slot->current_note = 60U;
    multi_voice_dsp_init_filter(slot,
                                MULTI_VOICE_DSP_FORMAT_MONO,
                                slot->sample_rate);
    env_adsr_init(&slot->filter_env, slot->sample_rate);
    env_adsr_init(&slot->vca_env, slot->sample_rate);
    env_adsr_reset(&slot->filter_env);
    env_adsr_reset(&slot->vca_env);
}

void multi_voice_dsp_init(void)
{
    multi_voice_dsp_reset();
}

void multi_voice_dsp_reset(void)
{
    for (uint8_t i = 0U; i < MULTI_VOICE_DSP_SLOT_COUNT; ++i)
    {
        multi_voice_dsp_reset_slot(&g_multi_voice_dsp_pool[i]);
    }
}

multi_voice_dsp_slot_t *multi_voice_dsp_get(uint8_t slot_index)
{
    return (slot_index < MULTI_VOICE_DSP_SLOT_COUNT)
               ? &g_multi_voice_dsp_pool[slot_index]
               : NULL;
}

const multi_voice_dsp_slot_t *multi_voice_dsp_get_const(uint8_t slot_index)
{
    return (slot_index < MULTI_VOICE_DSP_SLOT_COUNT)
               ? &g_multi_voice_dsp_pool[slot_index]
               : NULL;
}

uint8_t multi_voice_dsp_find_slot(brick6_sampler_multi_voice_handle_t handle,
                                  uint8_t *out_slot_index)
{
    if ((out_slot_index == NULL) || (multi_voice_dsp_handle_is_valid(handle) == 0U))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < MULTI_VOICE_DSP_SLOT_COUNT; ++i)
    {
        const multi_voice_dsp_slot_t *const slot = &g_multi_voice_dsp_pool[i];
        if ((slot->state != (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE)
            && (slot->owner_voice_index == handle.voice_index)
            && (slot->owner_generation == handle.generation))
        {
            *out_slot_index = i;
            return 1U;
        }
    }
    return 0U;
}

uint8_t multi_voice_dsp_acquire(brick6_sampler_multi_voice_handle_t handle,
                                multi_voice_dsp_format_t format,
                                float sample_rate,
                                uint8_t *out_slot_index)
{
    if ((out_slot_index == NULL)
        || (multi_voice_dsp_handle_is_valid(handle) == 0U)
        || (format > MULTI_VOICE_DSP_FORMAT_STEREO))
    {
        return 0U;
    }

    uint8_t existing_slot = MULTI_VOICE_DSP_SLOT_INDEX_INVALID;
    if (multi_voice_dsp_find_slot(handle, &existing_slot) != 0U)
    {
        *out_slot_index = existing_slot;
        return 0U;
    }

    if (!(sample_rate > 0.0f))
    {
        sample_rate = MULTI_VOICE_DSP_DEFAULT_SAMPLE_RATE;
    }

    for (uint8_t i = 0U; i < MULTI_VOICE_DSP_SLOT_COUNT; ++i)
    {
        multi_voice_dsp_slot_t *const slot = &g_multi_voice_dsp_pool[i];
        if (slot->state != (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE)
        {
            continue;
        }

        multi_voice_dsp_reset_slot(slot);
        slot->owner_voice_index = handle.voice_index;
        slot->owner_generation = handle.generation;
        slot->state = (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_HELD;
        slot->sample_rate = sample_rate;
        multi_voice_dsp_init_filter(slot, format, sample_rate);
        env_adsr_init(&slot->filter_env, sample_rate);
        env_adsr_init(&slot->vca_env, sample_rate);
        env_adsr_reset(&slot->filter_env);
        env_adsr_reset(&slot->vca_env);
        *out_slot_index = i;
        return 1U;
    }

    return 0U;
}

uint8_t multi_voice_dsp_release(uint8_t slot_index,
                                brick6_sampler_multi_voice_handle_t handle)
{
    if ((slot_index >= MULTI_VOICE_DSP_SLOT_COUNT)
        || (multi_voice_dsp_handle_is_valid(handle) == 0U))
    {
        return 0U;
    }

    multi_voice_dsp_slot_t *const slot = &g_multi_voice_dsp_pool[slot_index];
    if ((slot->state == (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE)
        || (slot->owner_voice_index != handle.voice_index)
        || (slot->owner_generation != handle.generation))
    {
        return 0U;
    }

    multi_voice_dsp_reset_slot(slot);
    return 1U;
}

uint8_t multi_voice_dsp_validate_ownership(void)
{
    for (uint8_t i = 0U; i < MULTI_VOICE_DSP_SLOT_COUNT; ++i)
    {
        const multi_voice_dsp_slot_t *const slot = &g_multi_voice_dsp_pool[i];
        if (slot->state == (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE)
        {
            if ((slot->owner_voice_index != BRICK6_SAMPLER_MULTI_VOICE_INDEX_INVALID)
                || (slot->owner_generation != 0U))
            {
                return 0U;
            }
            continue;
        }

        if ((slot->owner_voice_index >= MULTI_VOICE_DSP_SLOT_COUNT)
            || (slot->owner_generation == 0U))
        {
            return 0U;
        }

        for (uint8_t j = (uint8_t)(i + 1U); j < MULTI_VOICE_DSP_SLOT_COUNT; ++j)
        {
            const multi_voice_dsp_slot_t *const other = &g_multi_voice_dsp_pool[j];
            if ((other->state != (uint8_t)BRICK6_SAMPLER_MULTI_VOICE_FREE)
                && (other->owner_voice_index == slot->owner_voice_index)
                && (other->owner_generation == slot->owner_generation))
            {
                return 0U;
            }
        }
    }
    return 1U;
}
