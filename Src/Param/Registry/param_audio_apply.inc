uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value)
{
    /* RT fast path: same value semantics as apply_track_value, but restricted to modulation callers. */
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = param_value_policy_canonicalize(prepared.id, track,
                                                          prepared.value);

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
    {
        return 0U;
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value_audio(id, track, clamped);
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

uint8_t param_registry_apply_track_value_audio(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }
    return param_registry_apply_prepared_track_value_audio(&prepared, track);
}

uint8_t param_registry_apply_prepared_track_value_audio(
    const param_registry_prepared_value_t *prepared,
    uint8_t track)
{
    if ((prepared == NULL)
            || (prepared->id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(prepared->id) != 0U))
    {
        return 0U;
    }

    const param_id_t id = prepared->id;
    const float clamped = param_value_policy_canonicalize(id, track,
                                                           prepared->value);

    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        const float audio_value = ((id == PARAM_AUDIO_FX_P3)
                || (id == PARAM_AUDIO_FX_B_P3))
            ? param_registry_audio_fx_clamp_p3(clamped) : clamped;
        track_audio_runtime_ctx_t audio_ctx;
        if ((audio_note_engine_adapter_current_ctx(track, &audio_ctx) == 0U)
                || (audio_note_engine_adapter_ctx_is_audio_routable(&audio_ctx) == 0U))
        {
            return 0U;
        }
        if (audio_fx_runtime_apply_param((brick_entity_id_t)track,
                                         id,
                                         audio_value) == 0U)
        {
            return 0U;
        }

        return 1U;
    }

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
        return 0U;

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t applied = param_filter_apply_value_audio(
            id, track, clamped);
        return applied;
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            const uint8_t applied = mod_env3_audio_apply_track_param(track, env_param, clamped);
            return applied;
        }
    }

    if (id == PARAM_ENV_RETRIG_MOD)
    {
        mod_env3_audio_apply_retrigger(track, clamped);
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG))
    {
        return 0U;
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

static uint8_t param_registry_publish_track_value_runtime_temp(
    param_id_t id, uint8_t track, float value)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(id) != 0U))
        return 0U;
    const float clamped = clamp_value(
        value, param_registry[id].min, param_registry[id].max);
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 1U,
        .item = {{
            .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS
                                | LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP),
            .value = live_parameter_event_encode_float(clamped)
        }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

uint8_t param_registry_project_track_base_audio(param_id_t id,
                                                uint8_t track,
                                                float value)
{
    return param_registry_publish_track_value_runtime_temp(id, track, value);
}


static uint8_t param_registry_audio_fx_valid_model(uint8_t model)
{
    return (uint8_t)((model == AUDIO_FX_MODEL_LOFI)
        || (model == AUDIO_FX_MODEL_FOLD) || (model == AUDIO_FX_MODEL_DRIVE)
        || (model == AUDIO_FX_MODEL_POINT) || (model == AUDIO_FX_MODEL_SUB)
        || (model == AUDIO_FX_MODEL_SUB_LIGHT) || (model == AUDIO_FX_MODEL_RING)
        || (model == AUDIO_FX_MODEL_VIBE) || (model == AUDIO_FX_MODEL_DRIFT));
}

static void param_registry_audio_fx_set_control(uint8_t track,
                                                param_id_t id,
                                                float value)
{
    if ((id != PARAM_AUDIO_FX_MODEL) && (id != PARAM_AUDIO_FX_B_MODEL))
    {
        if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
            value = param_registry_audio_fx_clamp_p3(value);
        else if ((id == PARAM_GROUP_FX_A_LEVEL) || (id == PARAM_GROUP_FX_B_LEVEL))
            value = clamp_value(value, 0.0f, 1.0f);
        param_registry_control_value_set(track, id, value);
        return;
    }

    const uint8_t bank_b = (uint8_t)(id == PARAM_AUDIO_FX_B_MODEL);
    const param_id_t other_id = bank_b ? PARAM_AUDIO_FX_MODEL : PARAM_AUDIO_FX_B_MODEL;
    float other = 0.0f;
    (void)param_registry_control_value_get(track, other_id, &other);
    uint8_t model = (uint8_t)(value + 0.5f);
    if (!param_registry_audio_fx_valid_model(model)) model = AUDIO_FX_MODEL_OFF;
    if ((model != AUDIO_FX_MODEL_OFF) && (model == (uint8_t)other))
        model = AUDIO_FX_MODEL_OFF;
    param_registry_control_value_set(track, id, (float)model);
}

uint8_t param_registry_apply_track_value_runtime_temp_audio(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = prepared.value;

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_apply_track_param_temp(track, lfo_index, lfo_param, clamped);
        }
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            return mod_env3_apply_track_param_temp(track, env_param, clamped);
        }
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value_audio(id, track, clamped);
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

uint8_t param_registry_clear_track_value_runtime_temp_audio(param_id_t id, uint8_t track)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        return mod_lfo_v1_clear_track_param_temp_audio(track, lfo_index, lfo_param);
    }

    mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
    if (param_env3_map(id, &env_param) != 0U)
    {
        return mod_env3_clear_track_param_temp_audio(track, env_param);
    }
    return 0U;
}

