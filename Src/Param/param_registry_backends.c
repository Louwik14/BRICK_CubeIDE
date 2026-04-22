#include "Param/param_registry_backends.h"

#include "Audio/drum_synth.h"
#include "Core/brick6_master_buffer.h"
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

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if (sample_pool_is_loaded((uint16_t)(param_backend_clamp_value(value, 0.0f, 63.0f) + 0.5f)) == 0U)
            {
                brick6_sampler_runtime_stop(track);
                return 0U;
            }
            if (state != NULL)
            {
                state->sample = param_backend_clamp_value(value, 0.0f, 63.0f);
            }
            brick6_sampler_runtime_set_sample(track, (uint16_t)(param_backend_clamp_value(value, 0.0f, 63.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_GAIN:
            if (state != NULL)
            {
                state->gain = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            brick6_sampler_runtime_set_gain(track, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_SAMPLER_START:
            if (state != NULL)
            {
                state->start = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            if (state != NULL)
            {
                state->end = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_end(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
            if (state != NULL)
            {
                state->mode = (float)(uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            }
            brick6_sampler_runtime_set_mode(track, (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_TUNE:
            if (state != NULL)
            {
                state->tune = param_backend_clamp_value(value, -24.0f, 24.0f);
            }
            brick6_sampler_runtime_set_tune(track, param_backend_clamp_value(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_IN:
            if (state != NULL)
            {
                state->fade_in = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_fade_in(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_FADE_OUT:
            if (state != NULL)
            {
                state->fade_out = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_fade_out(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            if (state != NULL)
            {
                state->slice_count = (float)idx;
            }
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
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
        if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_TRX_BD)
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
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_TRX_CLAVES)
                && (id >= PARAM_DRUM_TRX_CLAVES_PITCH)
                && (id <= PARAM_DRUM_TRX_CLAVES_DRIVE))
        {
            switch (id)
            {
                case PARAM_DRUM_TRX_CLAVES_PITCH:
                    state->trx_claves.pitch = value;
                    break;
                case PARAM_DRUM_TRX_CLAVES_INTERVAL:
                    state->trx_claves.interval = value;
                    break;
                case PARAM_DRUM_TRX_CLAVES_DECAY:
                    state->trx_claves.decay = value;
                    break;
                case PARAM_DRUM_TRX_CLAVES_BALANCE:
                    state->trx_claves.balance = value;
                    break;
                case PARAM_DRUM_TRX_CLAVES_DRIVE:
                    state->trx_claves.drive = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_TRX_HIHAT)
                && (id >= PARAM_DRUM_TRX_HIHAT_DECAY)
                && (id <= PARAM_DRUM_TRX_HIHAT_PEAK))
        {
            switch (id)
            {
                case PARAM_DRUM_TRX_HIHAT_DECAY:
                    state->trx_hihat.decay = value;
                    break;
                case PARAM_DRUM_TRX_HIHAT_METAL:
                    state->trx_hihat.metal = value;
                    break;
                case PARAM_DRUM_TRX_HIHAT_HP_TONE:
                    state->trx_hihat.hp_tone = value;
                    break;
                case PARAM_DRUM_TRX_HIHAT_LP_TONE:
                    state->trx_hihat.lp_tone = value;
                    break;
                case PARAM_DRUM_TRX_HIHAT_GAP:
                    state->trx_hihat.gap = value;
                    break;
                case PARAM_DRUM_TRX_HIHAT_PEAK:
                    state->trx_hihat.peak = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_KICK)
                && (id >= PARAM_DRUM_FM_KICK_PITCH)
                && (id <= PARAM_DRUM_FM_KICK_MOD_ENV_SYNC))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_KICK_PITCH:
                    state->fm_kick.pitch = value;
                    break;
                case PARAM_DRUM_FM_KICK_DECAY:
                    state->fm_kick.decay = value;
                    break;
                case PARAM_DRUM_FM_KICK_FM_AMOUNT:
                    state->fm_kick.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_KICK_PITCH_SWEEP:
                    state->fm_kick.pitch_sweep = value;
                    break;
                case PARAM_DRUM_FM_KICK_FEEDBACK:
                    state->fm_kick.feedback = value;
                    break;
                case PARAM_DRUM_FM_KICK_MOD_FREQ:
                    state->fm_kick.mod_freq = value;
                    break;
                case PARAM_DRUM_FM_KICK_MOD_DECAY:
                    state->fm_kick.mod_decay = value;
                    break;
                case PARAM_DRUM_FM_KICK_SWEEP_DECAY:
                    state->fm_kick.sweep_decay = value;
                    break;
                case PARAM_DRUM_FM_KICK_RATIO_MODE:
                    state->fm_kick.ratio_mode = value;
                    break;
                case PARAM_DRUM_FM_KICK_RATIO_INDEX:
                    state->fm_kick.ratio_index = value;
                    break;
                case PARAM_DRUM_FM_KICK_MOD_ENV_SYNC:
                    state->fm_kick.mod_env_sync = value;
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
        default:
            (void)track;
            return 0U;
    }
}

uint8_t param_backend_apply_mix_track(const track_runtime_ctx_t *ctx, uint8_t track, param_id_t id, float value)
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
            if (state != NULL)
            {
                state->mix_level = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            mixer_set_track_gain(ctx->mix_track_id, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        }

        case PARAM_MIX_PAN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->mix_pan = param_backend_clamp_value(value, -1.0f, 1.0f);
            }
            mixer_set_track_pan(ctx->mix_track_id, param_backend_clamp_value(value, -1.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND1:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->mix_send1 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 0U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND2:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
            {
                state->mix_send2 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 1U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_MUTE:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if (state != NULL)
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
                if (state != NULL)
                {
                    state->hybrid_gate = (value >= 0.5f) ? 1.0f : 0.0f;
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
            if (state != NULL)
            {
                state->vca_release = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_release(ctx->mix_track_id, param_filter_ui127_to_release_s(value));
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
