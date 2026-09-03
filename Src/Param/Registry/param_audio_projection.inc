static float clamp_value(float v, float lo, float hi);

static float param_registry_global_modfx_unit(float value)
{
    return clamp_value(value, 0.0f, 127.0f) * (1.0f / 127.0f);
}

static float param_registry_global_modfx_command(param_id_t id,
                                                  float value,
                                                  uint8_t model)
{
    const float unit = param_registry_global_modfx_unit(value);
    if (model == FX_MODFX_DAISY_STEREO)
    {
        switch (id)
        {
            case PARAM_MODFX_RATE:
            case PARAM_MODFX_RATE_B:
                return (value <= 61.0f)
                    ? (0.01f * powf(30.0f, value / 61.0f))
                    : (0.3f * powf(40.0f, (value - 61.0f) / 66.0f));
            case PARAM_MODFX_DEPTH:
            case PARAM_MODFX_DEPTH_B:
                return (value <= 123.0f)
                    ? (0.9f * value / 123.0f)
                    : (0.9f + 0.03f * (value - 123.0f) / 4.0f);
            case PARAM_MODFX_OFFSET:
            case PARAM_MODFX_DELAY_B:
                return (value <= 95.0f)
                    ? (0.75f * value / 95.0f)
                    : (0.75f + 0.25f * (value - 95.0f) / 32.0f);
            case PARAM_MODFX_FEEDBACK:
                return (value <= 64.0f)
                    ? ((value - 64.0f) * (1.0f / 64.0f))
                    : ((value - 64.0f) * (1.0f / 63.0f));
            case PARAM_MODFX_WIDTH:
                return (value <= 64.0f)
                    ? (value * (1.0f / 128.0f))
                    : (0.5f + (value - 64.0f) * (0.5f / 63.0f));
            default:
                break;
        }
    }
    switch (id)
    {
        case PARAM_MODFX_RATE: return 0.01f + 11.99f * unit;
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET: return unit;
        default: return value;
    }
}

uint8_t param_registry_prepare_global_audio_command(param_id_t id,
                                                    float canonical_value,
                                                    uint8_t modfx_model,
                                                    float *out_command_value)
{
    if ((out_command_value == NULL) || (id >= PARAM_COUNT))
        return 0U;

    *out_command_value = canonical_value;
    switch (id)
    {
        case PARAM_MODFX_RATE:
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET:
        case PARAM_MODFX_RATE_B:
        case PARAM_MODFX_DELAY_B:
        case PARAM_MODFX_DEPTH_B:
        case PARAM_MODFX_WIDTH:
        {
            *out_command_value = param_registry_global_modfx_command(
                id, canonical_value, modfx_model);
            break;
        }
        default:
            break;
    }
    return 1U;
}

static const param_id_t g_audio_fx_param_order[] = {
    PARAM_AUDIO_FX_MODEL,
    PARAM_AUDIO_FX_P1,
    PARAM_AUDIO_FX_P2,
    PARAM_AUDIO_FX_P3,
    PARAM_AUDIO_FX_B_MODEL,
    PARAM_AUDIO_FX_B_P1,
    PARAM_AUDIO_FX_B_P2,
    PARAM_AUDIO_FX_B_P3,
    PARAM_GROUP_FX_A_LEVEL,
    PARAM_GROUP_FX_B_LEVEL
};

uint8_t param_registry_is_audio_fx_param(param_id_t id)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_audio_fx_param_order)
                                       / sizeof(g_audio_fx_param_order[0])); ++i)
    {
        if (g_audio_fx_param_order[i] == id)
            return 1U;
    }
    return 0U;
}

param_id_t param_registry_get_audio_fx_param(uint8_t order)
{
    return (order < (uint8_t)(sizeof(g_audio_fx_param_order)
                              / sizeof(g_audio_fx_param_order[0])))
        ? g_audio_fx_param_order[order] : PARAM_COUNT;
}









static float clamp_value(float v, float lo, float hi);



static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast);

static uint8_t param_registry_submit_audio_value(param_id_t id,
                                                  uint8_t track,
                                                  float value,
                                                  uint8_t scope)
{
    /* This is an immediate CONTROL commit, not an ingress capture.  Its
     * publication must share the NOW/timeline authority used by rebuilds;
     * the captured path remains reserved for real ingress timestamps. */
    return control_rt_publish_param_now(
        (track < BRICK_ENTITY_CAPACITY) ? track : 0U,
        (uint16_t)id,
        (uint32_t)live_parameter_event_encode_float(value),
        scope) ? 1U : 0U;
}

uint8_t param_registry_publish_track_base_audio(param_id_t id,
                                                uint8_t track,
                                                float value)
{
    return param_registry_submit_audio_value(
        id, track, value, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
}
