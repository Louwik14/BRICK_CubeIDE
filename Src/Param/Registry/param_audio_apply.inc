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

