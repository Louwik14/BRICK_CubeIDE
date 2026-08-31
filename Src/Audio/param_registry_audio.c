#include "Param/param_audio.h"

#include <stddef.h>

#include "Audio/audio_fx_runtime.h"
#include "Param/audio_fx_param_catalog.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/metronome_runtime.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_filter_audio.h"
#include "Param/param_registry_backends.h"
#include "Param/param_spec.h"
#include "Seq/seq_types.h"

static uint8_t param_audio_lfo_map(param_id_t id,
                                   uint8_t *out_lfo,
                                   mod_lfo_param_t *out_param)
{
    static const param_id_t ids[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
        { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
        { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
        { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE }
    };
    if ((out_lfo == NULL) || (out_param == NULL)) return 0U;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        for (uint8_t param = 0U; param < MOD_LFO_PARAM_COUNT; ++param)
        {
            if (ids[lfo][param] == id)
            {
                *out_lfo = lfo;
                *out_param = (mod_lfo_param_t)param;
                return 1U;
            }
        }
    }
    return 0U;
}

static uint8_t param_audio_env_map(param_id_t id, mod_env3_param_t *out_param)
{
    if (out_param == NULL) return 0U;
    switch (id)
    {
        case PARAM_ENV3_ATTACK: *out_param = MOD_ENV3_PARAM_ATTACK; return 1U;
        case PARAM_ENV3_DECAY: *out_param = MOD_ENV3_PARAM_DECAY; return 1U;
        case PARAM_ENV3_SUSTAIN: *out_param = MOD_ENV3_PARAM_SUSTAIN; return 1U;
        case PARAM_ENV3_RELEASE: *out_param = MOD_ENV3_PARAM_RELEASE; return 1U;
        default: return 0U;
    }
}

static uint8_t param_audio_apply_non_filter(param_id_t id,
                                            uint8_t track,
                                            float value)
{
    uint8_t lfo = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_audio_lfo_map(id, &lfo, &lfo_param) != 0U)
        return mod_lfo_v1_set_track_param_audio(track, lfo, lfo_param, value);
    if ((id == PARAM_EXTERNAL_INPUT) || (id == PARAM_MIDI_PROGRAM))
        return 0U;
    track_audio_runtime_ctx_t ctx;
    if ((audio_note_engine_adapter_current_ctx(track, &ctx) == 0U)
            || (audio_note_engine_adapter_ctx_is_audio_routable(&ctx) == 0U))
        return 0U;
    return param_backend_apply_prepared_track_value_audio(track, id, value);
}

uint8_t param_audio_apply_track_rt(param_id_t id,
                                   uint8_t track,
                                   float value)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY)
            || (id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
        return 0U;
    if (param_spec_value_is_valid(id, value) == 0U) return 0U;
    if (param_filter_audio_is_param(id) != 0U)
        return param_filter_apply_value_audio(id, track, value);
    return param_audio_apply_non_filter(id, track, value);
}

uint8_t param_audio_apply_track(
    const param_audio_value_t *prepared,
    uint8_t track)
{
    if ((prepared == NULL) || (prepared->id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)) return 0U;
    const param_id_t id = prepared->id;
    const float value = prepared->value;
    if (param_spec_value_is_valid(id, value) == 0U) return 0U;
    if (id == PARAM_CFG_METRO)
    {
        metronome_runtime_set_level_u7((uint8_t)(value + 0.5f));
        return 1U;
    }
    uint8_t fx_slot = 0U;
    uint8_t fx_param = 0U;
    if (audio_fx_param_catalog_param_info(id, &fx_slot, &fx_param) != 0U)
        return audio_fx_runtime_apply_param((brick_entity_id_t)track, id, value);
    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD)) return 0U;
    if (param_filter_audio_is_param(id) != 0U)
        return param_filter_apply_value_audio(id, track, value);
    mod_env3_param_t env_param;
    if (param_audio_env_map(id, &env_param) != 0U)
        return mod_env3_audio_apply_track_param(track, env_param, value);
    if (id == PARAM_ENV_RETRIG_MOD)
    {
        mod_env3_audio_apply_retrigger(track, value);
        return 1U;
    }
    return param_audio_apply_non_filter(id, track, value);
}

uint8_t param_audio_apply_track_temp(
    param_id_t id, uint8_t track, float value)
{
    uint8_t lfo = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_audio_lfo_map(id, &lfo, &lfo_param) != 0U)
        return mod_lfo_v1_apply_track_param_temp(track, lfo, lfo_param, value);
    mod_env3_param_t env_param;
    if (param_audio_env_map(id, &env_param) != 0U)
        return mod_env3_apply_track_param_temp(track, env_param, value);
    if (param_filter_audio_is_param(id) != 0U)
        return param_filter_apply_value_audio(id, track, value);
    return param_audio_apply_non_filter(id, track, value);
}

uint8_t param_audio_clear_track_temp(
    param_id_t id, uint8_t track)
{
    uint8_t lfo = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_audio_lfo_map(id, &lfo, &lfo_param) != 0U)
        return mod_lfo_v1_clear_track_param_temp_audio(track, lfo, lfo_param);
    mod_env3_param_t env_param;
    if (param_audio_env_map(id, &env_param) != 0U)
        return mod_env3_clear_track_param_temp_audio(track, env_param);
    return 0U;
}
