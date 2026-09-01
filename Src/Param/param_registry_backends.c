#include "Param/param_registry_backends.h"

#include <stddef.h>

#include "Audio/audio_note_engine_adapter.h"

#include "Audio/drum_synth.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Param/md_model_catalog.h"
#include "Audio/vca_env.h"
#include "Param/param_filter_audio.h"
#include "Sampler/sample_page_cache_config.h"
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

static uint8_t param_backend_fm_store_ui(track_tone_fm_base_voice_t *base,
                                         param_id_t id,
                                         float value)
{
    if (base == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_FM_TRANSPOSE: base->transpose = (uint8_t)(param_backend_clamp_value(value + 24.0f, 0.0f, 48.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R1: base->pitch_rates[0] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R2: base->pitch_rates[1] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R3: base->pitch_rates[2] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R4: base->pitch_rates[3] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L1: base->pitch_levels[0] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L2: base->pitch_levels[1] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L3: base->pitch_levels[2] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L4: base->pitch_levels[3] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        default: return 0U;
    }
}

static float param_backend_fm_macro_unit(float value)
{
    return 0.5f + (0.5f * param_backend_clamp_value(value, -1.0f, 1.0f));
}

static uint8_t param_backend_is_vca_param(param_id_t id)
{
    return (uint8_t)((id == PARAM_VCA_ATTACK)
                     || (id == PARAM_VCA_DECAY)
                     || (id == PARAM_VCA_SUSTAIN)
                     || (id == PARAM_VCA_RELEASE)
                     || (id == PARAM_FILTER_MODE)
                     || (id == PARAM_ENV_RETRIG_VCA));
}

static uint8_t param_backend_clip_size_index(float value)
{
    const uint8_t index = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
    return (index <= 5U) ? index : 5U;
}

static uint16_t param_backend_clip_grain_size_value(uint8_t index)
{
    static const uint16_t values[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
    return values[(index <= 5U) ? index : 5U];
}

uint8_t param_backend_apply_tone_prism(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->program_route.instance_id;

    uint8_t osc = 0U;
    switch (id)
    {
        case PARAM_PRISM_OSC1_MODEL:
        case PARAM_PRISM_OSC2_MODEL:
        {
            osc = (id == PARAM_PRISM_OSC2_MODEL) ? 1U : 0U;
            const float clamped = param_backend_clamp_value(value, 0.0f, (float)BRICK6_PRISM_LAST_MODEL);
            brick6_braids_runtime_set_osc_edit(instance_id, osc, (float)(uint8_t)(clamped + 0.5f));
            return 1U;
        }
        case PARAM_PRISM_PITCH_MOD1:
        case PARAM_PRISM_PITCH_MOD2:
        {
            osc = (id == PARAM_PRISM_PITCH_MOD2) ? 1U : 0U;
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_osc_pitch_mod(instance_id, osc, clamped);
            return 1U;
        }
        case PARAM_PRISM_OSC1_PARAM1:
        case PARAM_PRISM_OSC2_PARAM1:
        {
            osc = (id == PARAM_PRISM_OSC2_PARAM1) ? 1U : 0U;
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_osc_timbre(instance_id, osc, clamped);
            return 1U;
        }
        case PARAM_PRISM_OSC1_AMOD:
        case PARAM_PRISM_OSC2_AMOD:
        {
            osc = (id == PARAM_PRISM_OSC2_AMOD) ? 1U : 0U;
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_osc_modulation(instance_id, osc, clamped);
            return 1U;
        }
        case PARAM_PRISM_OSC1_PARAM2:
        case PARAM_PRISM_OSC2_PARAM2:
        {
            osc = (id == PARAM_PRISM_OSC2_PARAM2) ? 1U : 0U;
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_osc_color(instance_id, osc, clamped);
            return 1U;
        }
        case PARAM_PRISM_PHASE1_RESET:
        {
            const float clamped = (value >= 0.5f) ? 1.0f : 0.0f;
            brick6_braids_runtime_set_phase_reset(instance_id, (clamped >= 0.5f) ? 1U : 0U);
            return 1U;
        }
        case PARAM_PRISM_VOLUME:
            value = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_volume(instance_id, value); return 1U;
        case PARAM_PRISM_BALANCE:
            value = param_backend_clamp_value(value, -1.0f, 1.0f);
            brick6_braids_runtime_set_balance(instance_id, value); return 1U;
        case PARAM_PRISM_TUNE:
            value = param_backend_clamp_value(value, -60.0f, 60.0f);
            brick6_braids_runtime_set_tune(instance_id, value); return 1U;
        case PARAM_PRISM_DETUNE:
            value = param_backend_clamp_value(value, -24.0f, 24.0f);
            brick6_braids_runtime_set_detune(instance_id, value); return 1U;
        case PARAM_PRISM_DRIFT:
            value = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_braids_runtime_set_drift(instance_id, value); return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_fm(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM))
    {
        return 0U;
    }

    if ((id >= PARAM_FM_UI_FIRST) && (id <= PARAM_FM_UI_LAST))
    {
            track_tone_fm_base_voice_t base;
            if ((brick6_fm_runtime_get_base_voice(ctx->program_route.instance_id, &base) == 0U)
                    || (param_backend_fm_store_ui(&base, id, value) == 0U))
                return 0U;
            brick6_fm_runtime_set_base_voice(ctx->program_route.instance_id, &base);
        return 1U;
    }

    if ((id >= PARAM_FM_PLAY_VEL) && (id <= PARAM_FM_PLAY_PITCH_TIME))
    {
        track_tone_fm_macros_t local_macros;
        if (brick6_fm_runtime_get_macros(ctx->program_route.instance_id,
                                                 &local_macros) == 0U)
            return 0U;
        const track_tone_fm_macros_t *const macros = &local_macros;
        float velocity = macros->play_vel;
        float key_scaling = macros->play_key;
        float pitch_env = macros->pitch_env;
        float pitch_time = macros->pitch_time;
        if (id == PARAM_FM_PLAY_VEL) velocity = param_backend_clamp_value(value, 0.0f, 1.0f);
        else if (id == PARAM_FM_PLAY_KEY) key_scaling = param_backend_clamp_value(value, 0.0f, 1.0f);
        else if (id == PARAM_FM_PLAY_PITCH_ENV) pitch_env = param_backend_clamp_value(value, -1.0f, 1.0f);
        else pitch_time = param_backend_clamp_value(value, 0.0f, 1.0f);
        brick6_fm_runtime_set_play(ctx->program_route.instance_id, velocity, key_scaling, pitch_env, pitch_time);
        return 1U;
    }

    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        const uint8_t operator_id = (uint8_t)(offset / PARAM_FM_OPERATOR_PARAM_COUNT);
        const brick6_fm_operator_param_t operator_param =
            (brick6_fm_operator_param_t)(offset % PARAM_FM_OPERATOR_PARAM_COUNT);
        brick6_fm_runtime_set_operator(ctx->program_route.instance_id, operator_id, operator_param, value);
        return 1U;
    }

    switch (id)
    {
        case PARAM_FM_RATIO:
        {
            const float ratio = param_backend_clamp_value(value, -1.0f, 1.0f);
            brick6_fm_runtime_set_ratio(ctx->program_route.instance_id, param_backend_fm_macro_unit(ratio));
            return 1U;
        }
        case PARAM_FM_ALGORITHM:
        {
            const uint8_t algorithm = (uint8_t)(param_backend_clamp_value(value, 0.0f, 31.0f) + 0.5f);
            brick6_fm_runtime_set_algorithm(ctx->program_route.instance_id, algorithm);
            return 1U;
        }
        case PARAM_FM_FEEDBACK:
        {
            const uint8_t feedback = (uint8_t)(param_backend_clamp_value(value, 0.0f, 7.0f) + 0.5f);
            brick6_fm_runtime_set_feedback(ctx->program_route.instance_id, feedback);
            return 1U;
        }
        case PARAM_FM_SYNC:
        {
            const uint8_t sync = (value >= 0.5f) ? 1U : 0U;
            brick6_fm_runtime_set_sync(ctx->program_route.instance_id, sync);
            return 1U;
        }
        case PARAM_FM_BRIGHT:
        case PARAM_FM_BODY:
        case PARAM_FM_DETAIL:
        case PARAM_FM_METAL:
        {
            const float macro = param_backend_clamp_value(value, -1.0f, 1.0f);
            if (id == PARAM_FM_BRIGHT) brick6_fm_runtime_set_bright(ctx->program_route.instance_id, param_backend_fm_macro_unit(macro));
            else if (id == PARAM_FM_BODY) brick6_fm_runtime_set_body(ctx->program_route.instance_id, param_backend_fm_macro_unit(macro));
            else if (id == PARAM_FM_DETAIL) brick6_fm_runtime_set_detail(ctx->program_route.instance_id, param_backend_fm_macro_unit(macro));
            else brick6_fm_runtime_set_metal(ctx->program_route.instance_id, param_backend_fm_macro_unit(macro));
            return 1U;
        }
        case PARAM_FM_ENV_ATTACK:
        case PARAM_FM_ENV_DECAY:
        case PARAM_FM_ENV_SUSTAIN:
        case PARAM_FM_ENV_RELEASE:
        {
            const float macro = param_backend_clamp_value(value, -1.0f, 1.0f);
            track_tone_fm_macros_t local_macros;
            if (brick6_fm_runtime_get_macros(ctx->program_route.instance_id,
                                                     &local_macros) == 0U)
                return 0U;
            const track_tone_fm_macros_t *const macros = &local_macros;
            float attack = macros->env_attack;
            float decay = macros->env_decay;
            float sustain = macros->env_sustain;
            float release = macros->env_release;
            if (id == PARAM_FM_ENV_ATTACK) attack = macro;
            else if (id == PARAM_FM_ENV_DECAY) decay = macro;
            else if (id == PARAM_FM_ENV_SUSTAIN) sustain = macro;
            else release = macro;
            brick6_fm_runtime_set_env(ctx->program_route.instance_id,
                                      param_backend_fm_macro_unit(attack),
                                      param_backend_fm_macro_unit(decay),
                                      param_backend_fm_macro_unit(sustain),
                                      param_backend_fm_macro_unit(release));
            return 1U;
        }
        default:
            return 0U;
    }
}

static uint8_t param_backend_stack_slot_for_id(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((out_slot == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    if ((id >= PARAM_STACK_OSC1_LEVEL) && (id <= PARAM_STACK_OSC3_LEVEL))
    {
        *out_slot = (uint8_t)(id - PARAM_STACK_OSC1_LEVEL);
        *out_param = 0U;
        return 1U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 4U);
        *out_param = (uint8_t)((rel % 4U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

uint8_t param_backend_apply_tone_stack(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->program_route.instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    if (id == PARAM_STACK_NOISE_LEVEL)
    {
        const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
        brick6_stack_runtime_set_noise_level(ctx->program_route.instance_id, clamped);
        return 1U;
    }
    if (id == PARAM_STACK_OSC_DETUNE)
    {
        const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
        brick6_stack_runtime_set_osc_detune(ctx->program_route.instance_id, clamped);
        return 1U;
    }
    if (id == PARAM_STACK_PHASE_RESET)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        brick6_stack_runtime_set_phase_reset(ctx->program_route.instance_id, enabled);
        return 1U;
    }

    uint8_t slot = 0U;
    uint8_t slot_param = 0U;
    if (param_backend_stack_slot_for_id(id, &slot, &slot_param) == 0U)
    {
        return 0U;
    }

    switch (slot_param)
    {
        case 0U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_stack_runtime_set_slot_level(ctx->program_route.instance_id, slot, clamped);
            return 1U;
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U));
            const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(clamped + 0.5f);
            brick6_stack_runtime_set_slot_model(ctx->program_route.instance_id, slot, model);
            return 1U;
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, -24.0f, 24.0f);
            brick6_stack_runtime_set_slot_tune(ctx->program_route.instance_id, slot, clamped);
            return 1U;
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_stack_runtime_set_slot_timbre(ctx->program_route.instance_id, slot, clamped);
            return 1U;
        }
        case 4U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_stack_runtime_set_slot_color(ctx->program_route.instance_id, slot, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
}

static uint8_t param_backend_wave_slot_for_id(param_id_t id, uint8_t *out_osc, uint8_t *out_param)
{
    if ((out_osc == NULL) || (out_param == NULL)) return 0U;
    if ((id >= PARAM_WAVE_OSC1_POS) && (id <= PARAM_WAVE_OSC1_LEN))
    {
        *out_osc = 0U;
        *out_param = (uint8_t)(id - PARAM_WAVE_OSC1_POS + 1U);
        return 1U;
    }
    if ((id >= PARAM_WAVE_OSC2_POS) && (id <= PARAM_WAVE_OSC2_LEN))
    {
        *out_osc = 1U;
        *out_param = (uint8_t)(id - PARAM_WAVE_OSC2_POS + 1U);
        return 1U;
    }
    return 0U;
}

uint8_t param_backend_apply_tone_wave(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->program_route.instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    if (id == PARAM_WAVE_VOLUME)
    {
        const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
        brick6_wave_runtime_set_volume(ctx->program_route.instance_id, clamped);
        return 1U;
    }
    if (id == PARAM_WAVE_BALANCE)
    {
        const float clamped = param_backend_clamp_value(value, -1.0f, 1.0f);
        brick6_wave_runtime_set_balance(ctx->program_route.instance_id, clamped);
        return 1U;
    }
    if (id == PARAM_WAVE_TUNE)
    {
        const float clamped = param_backend_clamp_value(value, -60.0f, 60.0f);
        brick6_wave_runtime_set_tune(ctx->program_route.instance_id, clamped);
        return 1U;
    }
    if (id == PARAM_WAVE_DETUNE)
    {
        const float clamped = param_backend_clamp_value(value, -24.0f, 24.0f);
        brick6_wave_runtime_set_detune(ctx->program_route.instance_id, clamped);
        return 1U;
    }

    uint8_t osc = 0U;
    uint8_t slot_param = 0U;
    if (param_backend_wave_slot_for_id(id, &osc, &slot_param) == 0U)
    {
        return 0U;
    }

    switch (slot_param)
    {
        case 0U:
        {
            (void)value;
            return 1U;
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_wave_runtime_set_osc_pos(ctx->program_route.instance_id, osc, clamped);
            return 1U;
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_wave_runtime_set_osc_start(ctx->program_route.instance_id, osc, clamped);
            return 1U;
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.01f, 1.0f);
            brick6_wave_runtime_set_osc_len(ctx->program_route.instance_id, osc, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;

    switch (id)
    {
        case PARAM_SAMPLER_GAIN:
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                const float gain = param_backend_clamp_value(value, 0.0f, 2.0f);
                brick6_sampler_runtime_set_multi_gain(track, gain);
                return 1U;
            }
            brick6_sampler_runtime_set_gain(track, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_SAMPLER_START:
            brick6_sampler_runtime_set_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_LENGTH:
            brick6_sampler_runtime_set_length(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
        {
            uint8_t mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            if (mode >= 4U)
            {
                mode = 0U;
            }
            brick6_sampler_runtime_set_mode(track, mode);
            return 1U;
        }
        case PARAM_SAMPLER_TUNE:
            brick6_sampler_runtime_set_tune(track, param_backend_clamp_value(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(param_backend_clamp_value(value, 0.0f, 6.0f) + 0.5f);
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_LOOP_START:
            brick6_sampler_runtime_set_loop_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_source_bpm(track, param_backend_clamp_value(value, 40.0f, 300.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_sync_length(track,
                                                        (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_pitch(track, param_backend_clamp_value(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_play_mode(track,
                                                      (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_loop(track,
                                                 (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        {
            const uint8_t stretch_mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f);

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_stretch_mode(track, stretch_mode);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            uint8_t grain_index;

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                return 0U;
            }

            grain_index = param_backend_clip_size_index(value);

            brick6_sampler_runtime_set_clip_grain_size(track, param_backend_clip_grain_size_value(grain_index));
            return 1U;
        }
        case PARAM_SAMPLER_MULTI_LOOP:
        {
            const uint8_t enabled =
                (param_backend_clamp_value(value, 0.0f, 1.0f) >= 0.5f) ? 1U : 0U;
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                return 0U;
            }
            brick6_sampler_runtime_set_multi_loop(track, enabled);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_looper(uint8_t track, param_id_t id, float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;

    if ((ctx == NULL)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_LOOPER_XFADE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_looper_runtime_set_main_xfade(track, clamped);
            return 1U;
        }
        case PARAM_LOOPER_STRETCH:
            brick6_looper_runtime_set_stretch_mode(
                track, (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f));
            return 1U;
        case PARAM_LOOPER_PITCH:
            brick6_looper_runtime_set_stretch_pitch(
                track, param_backend_clamp_value(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_LOOPER_GRAIN:
            brick6_looper_runtime_set_stretch_grain(
                track, param_backend_clip_grain_size_value(
                    (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f)));
            return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_drum(uint8_t track,
                                      const track_audio_runtime_ctx_t *ctx,
                                      param_id_t id,
                                      float value)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    return drum_synth_set_param_for_instance(ctx->program_route.instance_id, id, value);
}

uint8_t param_backend_apply_mix_track(const track_audio_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value)
{
    if ((ctx == NULL) || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U))
    {
        return 0U;
    }

    if ((param_backend_is_vca_param(id) != 0U)
            && (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_MIX_LEVEL:
        {
            mixer_set_track_gain(ctx->program_route.mix_track_id, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        }

        case PARAM_MIX_PAN:
        {
            mixer_set_track_pan(ctx->program_route.mix_track_id, param_backend_clamp_value(value, -1.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND1:
        {
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 0U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND2:
        {
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 1U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND3:
        {
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 2U,
                                       param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_MUTE:
            return audio_note_engine_adapter_set_mute(
                track, (value >= 0.5f) ? 1U : 0U);

        case PARAM_ENV_RETRIG_FILTER:
        {
            const uint8_t hard = (value >= 0.5f) ? 1U : 0U;
            if (((ctx->flags & 1U) != 0U)
                    && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS))
            {
                mixer_set_track_filter_retrigger_hard(
                    ctx->program_route.mix_track_id, hard);
            }
            return 1U;
        }

        case PARAM_ENV_RETRIG_VCA:
        {
            const uint8_t hard = (value >= 0.5f) ? 1U : 0U;
            mixer_set_track_vca_retrigger_hard(ctx->program_route.mix_track_id, hard);
            return 1U;
        }

        case PARAM_FILTER_MODE:
        {
            const uint8_t mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f);
            mixer_set_track_filter_mode(ctx->program_route.mix_track_id, mode);
            return 1U;
        }

        case PARAM_VCA_ATTACK:
        {
            mixer_set_track_vca_attack(ctx->program_route.mix_track_id, param_filter_audio_attack_s(value));
            return 1U;
        }

        case PARAM_VCA_DECAY:
        {
            mixer_set_track_vca_decay(ctx->program_route.mix_track_id, param_filter_audio_decay_s(value));
            return 1U;
        }

        case PARAM_VCA_SUSTAIN:
        {
            mixer_set_track_vca_sustain(ctx->program_route.mix_track_id, param_filter_audio_sustain(value));
            return 1U;
        }

        case PARAM_VCA_RELEASE:
        {
            const float release_s = param_filter_audio_release_s(value);
            mixer_set_track_vca_release(ctx->program_route.mix_track_id, release_s);
            if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->program_route.instance_id, release_s);
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

uint8_t param_backend_apply_env_track(const track_audio_runtime_ctx_t *ctx, param_id_t id, float value)
{
    if ((ctx == NULL) || (ctx->program_route.active == 0U))
    {
        return 0U;
    }

    switch (ctx->program_route.engine)
    {
        case (uint8_t)TRACK_RUNTIME_ENGINE_DRUM:
            return drum_synth_set_param_for_instance(ctx->program_route.instance_id, id, value);
        default:
            return 0U;
    }
}
