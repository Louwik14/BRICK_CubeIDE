#include "Param/param_registry_backends.h"

#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_master_buffer.h"
#include "Core/brick6_opal_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_sound_state.h"
#include "Param/param_filter.h"
#include "Sampler/sample_pool.h"
#include "midi.h"
#include "mixer.h"

static float param_backend_clamp_value(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static uint8_t param_backend_clip_size_index(float value)
{
    const uint8_t index = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
    return (index <= 5U) ? index : 5U;
}

static uint16_t param_backend_clip_size_value(uint8_t index)
{
    static const uint16_t values[] = {32U, 64U, 96U, 128U, 256U, 512U};
    return values[(index <= 5U) ? index : 5U];
}

static uint16_t param_backend_clip_grain_size_value(uint8_t index)
{
    static const uint16_t values[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
    return values[(index <= 5U) ? index : 5U];
}

static uint8_t param_backend_clip_search_index(float value)
{
    const uint8_t index = (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f);
    return (index <= 4U) ? index : 4U;
}

uint8_t param_backend_apply_tone_opal(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_OPAL))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;

    switch (id)
    {
        case PARAM_OPAL_PATCH:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->opal.patch = clamped;
            }
            brick6_opal_runtime_set_harmonics(instance_id, clamped);
            return 1U;
        }
        case PARAM_OPAL_INDEX:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->opal.index = clamped;
            }
            brick6_opal_runtime_set_timbre(instance_id, clamped);
            return 1U;
        }
        case PARAM_OPAL_TIME:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->opal.time = clamped;
            }
            brick6_opal_runtime_set_morph(instance_id, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_braids(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_BRAIDS))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;

    switch (id)
    {
        case PARAM_BRAIDS_EDIT:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 47.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.edit = (float)(uint8_t)(clamped + 0.5f);
            }
            brick6_braids_runtime_set_edit(instance_id, (float)(uint8_t)(clamped + 0.5f));
            return 1U;
        }
        case PARAM_BRAIDS_FINE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.fine = clamped;
            }
            brick6_braids_runtime_set_fine(instance_id, clamped);
            return 1U;
        }
        case PARAM_BRAIDS_COARSE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.coarse = clamped;
            }
            brick6_braids_runtime_set_coarse(instance_id, clamped);
            return 1U;
        }
        case PARAM_BRAIDS_FM:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.fm = clamped;
            }
            brick6_braids_runtime_set_fm(instance_id, clamped);
            return 1U;
        }
        case PARAM_BRAIDS_TIMBRE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.timbre = clamped;
            }
            brick6_braids_runtime_set_timbre(instance_id, clamped);
            return 1U;
        }
        case PARAM_BRAIDS_MODULATION:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.modulation = clamped;
            }
            brick6_braids_runtime_set_modulation(instance_id, clamped);
            return 1U;
        }
        case PARAM_BRAIDS_COLOR:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->braids.color = clamped;
            }
            brick6_braids_runtime_set_color(instance_id, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_is_midi_cc_id(param_id_t id)
{
    return ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)) ? 1U : 0U;
}

uint8_t param_backend_midi_cc_number_from_id(param_id_t id)
{
    if (param_backend_is_midi_cc_id(id) == 0U)
    {
        return 0U;
    }

    return (uint8_t)(16U + (uint8_t)(id - PARAM_MIDI_CC1_1));
}

uint8_t param_backend_track_supports_midi_tone_ctx(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
    {
        return 1U;
    }

    return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

uint8_t param_backend_track_supports_midi_tone_descriptor(const track_runtime_descriptor_t *descriptor)
{
    if (descriptor == NULL)
    {
        return 0U;
    }

    return ((descriptor->family == TRACK_RUNTIME_FAMILY_MIDI)
            || ((descriptor->family == TRACK_RUNTIME_FAMILY_INPUT)
                && (descriptor->type == TRACK_RUNTIME_TYPE_HYBRID))) ? 1U : 0U;
}

uint8_t param_backend_send_midi_cc(uint8_t track, param_id_t id, float value)
{
    if (param_backend_is_midi_cc_id(id) == 0U)
    {
        return 0U;
    }

    {
        const uint8_t cc_number = param_backend_midi_cc_number_from_id(id);
        const uint8_t cc_value = (uint8_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f);
        const uint8_t channel = track_runtime_get_midi_channel_zero_based(track);

        midi_cc(MIDI_DEST_BOTH, channel, cc_number, cc_value);
    }

    return 1U;
}

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if (sample_pool_is_loaded((uint16_t)(param_backend_clamp_value(value, 0.0f, 63.0f) + 0.5f)) == 0U)
            {
                brick6_sampler_runtime_stop(track);
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->sample = param_backend_clamp_value(value, 0.0f, 63.0f);
            }
            brick6_sampler_runtime_set_sample(track, (uint16_t)(param_backend_clamp_value(value, 0.0f, 63.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_GAIN:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->gain = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            brick6_sampler_runtime_set_gain(track, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_SAMPLER_START:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->start = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->end = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_end(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
        {
            uint8_t mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            if (mode >= 4U)
            {
                mode = 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mode = (float)mode;
            }
            brick6_sampler_runtime_set_mode(track, mode);
            return 1U;
        }
        case PARAM_SAMPLER_TUNE:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->tune = param_backend_clamp_value(value, -24.0f, 24.0f);
            }
            brick6_sampler_runtime_set_tune(track, param_backend_clamp_value(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_IN:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->fade_in = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_fade_in(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_OUT:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->fade_out = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_fade_out(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(param_backend_clamp_value(value, 0.0f, 6.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->slice_count = (float)idx;
            }
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.source_bpm = param_backend_clamp_value(value, 40.0f, 300.0f);
            }
            brick6_sampler_runtime_set_clip_source_bpm(track, param_backend_clamp_value(value, 40.0f, 300.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.sync_length = param_backend_clamp_value(value, 0.0f, 4.0f);
            }
            brick6_sampler_runtime_set_clip_sync_length(track,
                                                        (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.pitch = param_backend_clamp_value(value, -12.0f, 12.0f);
            }
            brick6_sampler_runtime_set_clip_pitch(track, param_backend_clamp_value(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.play_mode = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_clip_play_mode(track,
                                                      (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.loop = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_clip_loop(track,
                                                 (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        {
            const uint8_t stretch_mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f);

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.stretch_mode = (float)stretch_mode;
            }
            brick6_sampler_runtime_set_clip_stretch_mode(track, stretch_mode);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            uint8_t grain_index;

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            grain_index = param_backend_clip_size_index(value);

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.grain_size = (float)grain_index;
            }

            brick6_sampler_runtime_set_clip_grain_size(track, param_backend_clip_grain_size_value(grain_index));
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_HOP:
        {
            uint8_t grain_index;
            uint8_t hop_index;

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            grain_index = (state != NULL) ? param_backend_clip_size_index(state->clip.grain_size) : 4U;
            hop_index = param_backend_clip_size_index(value);
            if (param_backend_clip_size_value(hop_index) > param_backend_clip_size_value(grain_index))
            {
                hop_index = grain_index;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.grain_size = (float)grain_index;
                state->clip.hop_size = (float)hop_index;
            }
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SEARCH:
        {
            const uint8_t search_index = param_backend_clip_search_index(value);

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.search_size = (float)search_index;
            }
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_looper(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);

    if ((ctx == NULL)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        return 0U;
    }

    if ((update_base_state == 0U) || (state == NULL))
    {
        return ((id == PARAM_LOOPER_ARM) || (id == PARAM_LOOPER_LEN) || (id == PARAM_LOOPER_PLAY)) ? 1U : 0U;
    }

    switch (id)
    {
        case PARAM_LOOPER_ARM:
            state->looper.arm = param_backend_clamp_value(value, 0.0f, 2.0f);
            return 1U;
        case PARAM_LOOPER_LEN:
            state->looper.len = param_backend_clamp_value(value, 0.0f, 5.0f);
            return 1U;
        case PARAM_LOOPER_PLAY:
            state->looper.play = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_looper_runtime_set_play_auto(track, (state->looper.play >= 0.5f) ? 1U : 0U);
            return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_drum(uint8_t track,
                                      const track_runtime_ctx_t *ctx,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    if (update_base_state != 0U)
    {
        track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
        if ((state != NULL)
                && ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_TRX_BD)
                    || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
                && (id >= PARAM_DRUM_TRX_BD_PITCH)
                && (id <= PARAM_DRUM_TRX_BD_DRIVE))
        {
            switch (id)
            {
                case PARAM_DRUM_TRX_BD_PITCH:
                    state->trx_bd.pitch = value;
                    break;
                case PARAM_DRUM_TRX_BD_DECAY:
                    state->trx_bd.decay = value;
                    break;
                case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
                    state->trx_bd.pitch_sweep = value;
                    break;
                case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
                    state->trx_bd.sweep_decay = value;
                    break;
                case PARAM_DRUM_TRX_BD_ATTACK:
                    state->trx_bd.attack = value;
                    break;
                case PARAM_DRUM_TRX_BD_NOISE:
                    state->trx_bd.noise = value;
                    break;
                case PARAM_DRUM_TRX_BD_HARMONICS:
                    state->trx_bd.harmonics = value;
                    break;
                case PARAM_DRUM_TRX_BD_DRIVE:
                    state->trx_bd.drive = value;
                    break;
                default:
                    break;
            }
        }
    }

    return drum_synth_set_param_for_instance(ctx->instance_id, id, value);
}

uint8_t param_backend_apply_buffer_track(const track_runtime_ctx_t *ctx, uint8_t track, param_id_t id, float value)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);

    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_BUFFER))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_BUFFER_REC_LEN:
            brick6_master_buffer_set_record_len((uint32_t)(param_backend_clamp_value(value, 1.0f, 64.0f) + 0.5f));
            return 1U;
        case PARAM_BUFFER_Q_REC:
            brick6_master_buffer_set_quantize_record((value >= 0.5f) ? 1U : 0U);
            return 1U;
        case PARAM_BUFFER_Q_PLAY:
            brick6_master_buffer_set_quantize_play((value >= 0.5f) ? 1U : 0U);
            return 1U;
        case PARAM_BUFFER_RATE:
            brick6_master_buffer_set_rate(value);
            return 1U;
        case PARAM_BUFFER_FADE_IN:
            brick6_master_buffer_set_fade_in((uint32_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f));
            return 1U;
        case PARAM_BUFFER_FADE_OUT:
            brick6_master_buffer_set_fade_out((uint32_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f));
            return 1U;
        case PARAM_BUFFER_XFADE:
            brick6_master_buffer_set_xfade(param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_BUFFER_GRAIN:
        case PARAM_BUFFER_PRESERVE_PITCH:
        {
            brick6_master_buffer_shifter_config_t config;
            brick6_master_buffer_get_shifter_config(&config);

            switch (id)
            {
                case PARAM_BUFFER_GRAIN:
                    config.grain_size = param_backend_clip_grain_size_value(param_backend_clip_size_index(value));
                    if (state != NULL)
                    {
                        state->buffer.grain_size = param_backend_clamp_value(value, 0.0f, 5.0f);
                    }
                    break;
                case PARAM_BUFFER_PRESERVE_PITCH:
                    config.preserve_pitch = (value >= 0.5f) ? 1U : 0U;
                    if (state != NULL)
                    {
                        state->buffer.preserve_pitch = (float)config.preserve_pitch;
                    }
                    break;
                default:
                    break;
            }

            brick6_master_buffer_set_shifter_config(&config);
            return 1U;
        }
        default:
            (void)track;
            return 0U;
    }
}

uint8_t param_backend_apply_master_fx_track(const track_runtime_ctx_t *ctx,
                                            uint8_t track,
                                            param_id_t id,
                                            float value,
                                            uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const uint8_t slot = (uint8_t)((id - PARAM_MASTER_FX1_TYPE) / 4U);

    if ((ctx == NULL)
            || (state == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX)
            || (id < PARAM_MASTER_FX1_TYPE)
            || (id > PARAM_MASTER_FX4_B)
            || (slot >= 4U))
    {
        return 0U;
    }

    if (update_base_state == 0U)
    {
        return 1U;
    }

    switch ((uint8_t)((id - PARAM_MASTER_FX1_TYPE) % 4U))
    {
        case 0U:
            state->master_fx.type[slot] = param_backend_clamp_value(value, 0.0f, 12.0f);
            return 1U;
        case 1U:
            state->master_fx.level[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 2U:
            state->master_fx.macro_a[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 3U:
            state->master_fx.macro_b[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_mix_track(const track_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state)
{
    if ((ctx == NULL) || (track_runtime_is_audio_routable(track) == 0U))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_MIX_LEVEL:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_level = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            mixer_set_track_gain(ctx->mix_track_id, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        }

        case PARAM_MIX_PAN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_pan = param_backend_clamp_value(value, -1.0f, 1.0f);
            }
            mixer_set_track_pan(ctx->mix_track_id, param_backend_clamp_value(value, -1.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND1:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send1 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 0U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND2:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send2 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 1U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_MUTE:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_mute = (value >= 0.5f) ? 1.0f : 0.0f;
            }
            mixer_set_track_mute(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            return 1U;
        }

        case PARAM_HYBRID_GATE:
            if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))
            {
                return 0U;
            }
            {
                track_sound_state_t *state = track_sound_state_get(track);
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->input.hybrid_gate = (value >= 0.5f) ? 1.0f : 0.0f;
                }
            }
            mixer_set_track_vca_enabled(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            if (value < 0.5f)
            {
                mixer_track_vca_all_notes_off((uint32_t)ctx->mix_track_id);
            }
            return 1U;

        case PARAM_VCA_ATTACK:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->vca_attack = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_attack(ctx->mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        }

        case PARAM_VCA_DECAY:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->vca_decay = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_decay(ctx->mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        }

        case PARAM_VCA_SUSTAIN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->vca_sustain = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_sustain(ctx->mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        }

        case PARAM_VCA_RELEASE:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            const float release_s = param_filter_ui127_to_release_s(value);
            if (state != NULL)
            {
                state->vca_release = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_release(ctx->mix_track_id, release_s);
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_BRAIDS)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->instance_id, release_s);
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

uint8_t param_backend_apply_colors_track(const track_runtime_ctx_t *ctx, param_id_t id, float value)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    switch (ctx->engine)
    {
        case (uint8_t)TRACK_RUNTIME_ENGINE_DRUM:
            return drum_synth_set_param_for_instance(ctx->instance_id, id, value);
        default:
            return 0U;
    }
}
