#include "Param/param_registry_backends.h"

#include "Audio/drum_synth.h"
#include "Core/brick6_master_buffer.h"
#include "Core/brick6_plaits_runtime.h"
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

uint8_t param_backend_apply_tone_plaits(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_PLAITS))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;

    switch (id)
    {
        case PARAM_PLAITS_MODEL:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 21.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.model = (float)(uint8_t)(clamped + 0.5f);
            }
            brick6_plaits_runtime_set_model(instance_id, (float)(uint8_t)(clamped + 0.5f));
            return 1U;
        }
        case PARAM_PLAITS_COARSE_FREQUENCY:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.coarse_frequency = clamped;
            }
            brick6_plaits_runtime_set_coarse_frequency(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_HARMONICS:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.harmonics = clamped;
            }
            brick6_plaits_runtime_set_harmonics(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_TIMBRE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.timbre = clamped;
            }
            brick6_plaits_runtime_set_timbre(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_MORPH:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.morph = clamped;
            }
            brick6_plaits_runtime_set_morph(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_LPG_RESPONSE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.lpg_response = clamped;
            }
            brick6_plaits_runtime_set_lpg_response(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_DECAY:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.decay = clamped;
            }
            brick6_plaits_runtime_set_decay(instance_id, clamped);
            return 1U;
        }
        case PARAM_PLAITS_FREQUENCY_RANGE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->plaits.frequency_range = clamped;
            }
            brick6_plaits_runtime_set_frequency_range(instance_id, clamped);
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
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mode = (float)(uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            }
            brick6_sampler_runtime_set_mode(track, (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f));
            return 1U;
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
            static const uint8_t counts[] = {2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
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
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_SNARE)
                && (id >= PARAM_DRUM_FM_SNARE_PITCH)
                && (id <= PARAM_DRUM_FM_SNARE_NOISE_DECAY))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_SNARE_PITCH:
                    state->fm_snare.pitch = value;
                    break;
                case PARAM_DRUM_FM_SNARE_DECAY:
                    state->fm_snare.decay = value;
                    break;
                case PARAM_DRUM_FM_SNARE_FM_AMOUNT:
                    state->fm_snare.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_SNARE_NOISE:
                    state->fm_snare.noise = value;
                    break;
                case PARAM_DRUM_FM_SNARE_HP_TONE:
                    state->fm_snare.hp_tone = value;
                    break;
                case PARAM_DRUM_FM_SNARE_MOD_FREQ:
                    state->fm_snare.mod_freq = value;
                    break;
                case PARAM_DRUM_FM_SNARE_MOD_DECAY:
                    state->fm_snare.mod_decay = value;
                    break;
                case PARAM_DRUM_FM_SNARE_NOISE_DECAY:
                    state->fm_snare.noise_decay = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_TOM)
                && (id >= PARAM_DRUM_FM_TOM_PITCH)
                && (id <= PARAM_DRUM_FM_TOM_START_PHASE))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_TOM_PITCH:
                    state->fm_tom.pitch = value;
                    break;
                case PARAM_DRUM_FM_TOM_DECAY:
                    state->fm_tom.decay = value;
                    break;
                case PARAM_DRUM_FM_TOM_PITCH_SWEEP:
                    state->fm_tom.pitch_sweep = value;
                    break;
                case PARAM_DRUM_FM_TOM_FM_AMOUNT:
                    state->fm_tom.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_TOM_MOD_FREQ:
                    state->fm_tom.mod_freq = value;
                    break;
                case PARAM_DRUM_FM_TOM_MOD_DECAY:
                    state->fm_tom.mod_decay = value;
                    break;
                case PARAM_DRUM_FM_TOM_SWEEP_DECAY:
                    state->fm_tom.sweep_decay = value;
                    break;
                case PARAM_DRUM_FM_TOM_START_PHASE:
                    state->fm_tom.start_phase = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_RIMSHOT)
                && (id >= PARAM_DRUM_FM_RIMSHOT_RIM_PITCH)
                && (id <= PARAM_DRUM_FM_RIMSHOT_MOD_DECAY))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_RIMSHOT_RIM_PITCH:
                    state->fm_rimshot.rim_pitch = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_RIM_DECAY:
                    state->fm_rimshot.rim_decay = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_BODY_MIX:
                    state->fm_rimshot.body_mix = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_HP_TONE:
                    state->fm_rimshot.hp_tone = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT:
                    state->fm_rimshot.rim_fm_amount = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_BODY_PITCH:
                    state->fm_rimshot.body_pitch = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_BODY_DECAY:
                    state->fm_rimshot.body_decay = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT:
                    state->fm_rimshot.body_fm_amount = value;
                    break;
                case PARAM_DRUM_FM_RIMSHOT_MOD_DECAY:
                    state->fm_rimshot.mod_decay = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_CLAP)
                && (id >= PARAM_DRUM_FM_CLAP_CLAP_COUNT)
                && (id <= PARAM_DRUM_FM_CLAP_CLAP_DECAY))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_CLAP_CLAP_COUNT:
                    state->fm_clap.clap_count = value;
                    break;
                case PARAM_DRUM_FM_CLAP_CLAP_SPACING:
                    state->fm_clap.clap_spacing = value;
                    break;
                case PARAM_DRUM_FM_CLAP_TAIL_DECAY:
                    state->fm_clap.tail_decay = value;
                    break;
                case PARAM_DRUM_FM_CLAP_HP_TONE:
                    state->fm_clap.hp_tone = value;
                    break;
                case PARAM_DRUM_FM_CLAP_FEEDBACK:
                    state->fm_clap.feedback = value;
                    break;
                case PARAM_DRUM_FM_CLAP_FM_AMOUNT:
                    state->fm_clap.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_CLAP_BASE_FREQ:
                    state->fm_clap.base_freq = value;
                    break;
                case PARAM_DRUM_FM_CLAP_MOD_FREQ:
                    state->fm_clap.mod_freq = value;
                    break;
                case PARAM_DRUM_FM_CLAP_MOD_DECAY:
                    state->fm_clap.mod_decay = value;
                    break;
                case PARAM_DRUM_FM_CLAP_CLAP_DECAY:
                    state->fm_clap.clap_decay = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_COWBELL)
                && (id >= PARAM_DRUM_FM_COWBELL_PITCH)
                && (id <= PARAM_DRUM_FM_COWBELL_MOD_FREQ))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_COWBELL_PITCH:
                    state->fm_cowbell.pitch = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_DECAY_SHORT:
                    state->fm_cowbell.decay_short = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_DECAY_LONG:
                    state->fm_cowbell.decay_long = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_FM_AMOUNT:
                    state->fm_cowbell.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_FEEDBACK:
                    state->fm_cowbell.feedback = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_ENV_MIX:
                    state->fm_cowbell.env_mix = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_MOD_DECAY:
                    state->fm_cowbell.mod_decay = value;
                    break;
                case PARAM_DRUM_FM_COWBELL_MOD_FREQ:
                    state->fm_cowbell.mod_freq = value;
                    break;
                default:
                    break;
            }
        }
        else if ((state != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_FM_CYMBAL)
                && (id >= PARAM_DRUM_FM_CYMBAL_DECAY)
                && (id <= PARAM_DRUM_FM_CYMBAL_MOD_DECAY))
        {
            switch (id)
            {
                case PARAM_DRUM_FM_CYMBAL_DECAY:
                    state->fm_cymbal.decay = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_SUSTAIN:
                    state->fm_cymbal.sustain = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_FM_AMOUNT:
                    state->fm_cymbal.fm_amount = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_HP_TONE:
                    state->fm_cymbal.hp_tone = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_FEEDBACK:
                    state->fm_cymbal.feedback = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_BASE_CARRIER:
                    state->fm_cymbal.base_carrier = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_BASE_MOD:
                    state->fm_cymbal.base_mod = value;
                    break;
                case PARAM_DRUM_FM_CYMBAL_MOD_DECAY:
                    state->fm_cymbal.mod_decay = value;
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
    uint16_t grain_size = 256U;
    uint16_t hop_size = 128U;

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
        case PARAM_BUFFER_TSTR:
        case PARAM_BUFFER_GRAIN:
        case PARAM_BUFFER_HOP:
        case PARAM_BUFFER_QUALITY:
        case PARAM_BUFFER_SYNC_LEN:
        case PARAM_BUFFER_SRC_BPM:
        case PARAM_BUFFER_RATIO_Q16:
        case PARAM_BUFFER_TSNS:
        case PARAM_BUFFER_PRESERVE_PITCH:
        {
            brick6_master_buffer_stretch_config_t config;
            brick6_master_buffer_get_stretch_config(&config);

            switch (id)
            {
                case PARAM_BUFFER_TSTR:
                    config.stretch_mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f);
                    if (state != NULL)
                    {
                        state->buffer.stretch_mode = (float)config.stretch_mode;
                    }
                    break;
                case PARAM_BUFFER_GRAIN:
                    switch ((uint8_t)(param_backend_clamp_value(value, 0.0f, 3.0f) + 0.5f))
                    {
                        case 0U: grain_size = 128U; break;
                        case 1U: grain_size = 256U; break;
                        case 2U: grain_size = 384U; break;
                        case 3U: grain_size = 512U; break;
                        default: grain_size = 256U; break;
                    }
                    config.grain_size = grain_size;
                    if (state != NULL)
                    {
                        state->buffer.grain_size = param_backend_clamp_value(value, 0.0f, 3.0f);
                    }
                    break;
                case PARAM_BUFFER_HOP:
                    switch ((uint8_t)(param_backend_clamp_value(value, 0.0f, 3.0f) + 0.5f))
                    {
                        case 0U: hop_size = 64U; break;
                        case 1U: hop_size = 128U; break;
                        case 2U: hop_size = 192U; break;
                        case 3U: hop_size = 256U; break;
                        default: hop_size = 128U; break;
                    }
                    config.hop_size = hop_size;
                    if (state != NULL)
                    {
                        state->buffer.hop_size = param_backend_clamp_value(value, 0.0f, 3.0f);
                    }
                    break;
                case PARAM_BUFFER_QUALITY:
                    break;
                case PARAM_BUFFER_SYNC_LEN:
                    config.sync_len = (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f);
                    if (state != NULL)
                    {
                        state->buffer.sync_len = (float)config.sync_len;
                    }
                    break;
                case PARAM_BUFFER_SRC_BPM:
                    config.source_bpm_milli = (uint32_t)(param_backend_clamp_value(value, 40.0f, 300.0f) * 1000.0f);
                    if (state != NULL)
                    {
                        state->buffer.source_bpm = ((float)config.source_bpm_milli) * 0.001f;
                    }
                    break;
                case PARAM_BUFFER_RATIO_Q16:
                    config.ratio_q16 = (uint32_t)(param_backend_clamp_value(value, 16384.0f, 262144.0f) + 0.5f);
                    if (state != NULL)
                    {
                        state->buffer.ratio_q16 = (float)config.ratio_q16;
                    }
                    break;
                case PARAM_BUFFER_TSNS:
                    config.transient_sensitivity = (uint8_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f);
                    if (state != NULL)
                    {
                        state->buffer.transient_sensitivity = (float)config.transient_sensitivity;
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

            brick6_master_buffer_set_stretch_config(&config);
            return 1U;
        }
        default:
            (void)track;
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
