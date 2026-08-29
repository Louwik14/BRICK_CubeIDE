static float clamp_value(float v, float lo, float hi);

static uint8_t param_registry_audio_fx_pre_filter_supported_control(uint8_t track)
{
    track_runtime_descriptor_t descriptor;
    if (track_runtime_get_descriptor(track, &descriptor) == 0U
            || descriptor.active == 0U)
        return 0U;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get((brick_entity_id_t)track, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
        return 1U;
    if ((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor.type == TRACK_RUNTIME_TYPE_MULTI))
        return 0U;
    if (descriptor.family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        float voices = 1.0f;
        (void)param_registry_control_value_get(
            track, PARAM_CFG_POLY_VOICES, &voices);
        if (voices > 1.5f)
            return 0U;
    }
    return 1U;
}
static float param_registry_audio_fx_clamp_p3(float value)
{
    return clamp_value(value, 0.0f, 127.0f);
}
static void param_registry_audio_fx_set_control(uint8_t track,
                                                param_id_t id,
                                                float value);

static float param_registry_global_delay_time_seconds(float value)
{
    static const float beats[] = {
        0.125f, 0.1666667f, 0.25f, 0.3333333f, 0.5f, 0.6666667f,
        0.75f, 1.0f, 1.3333334f, 1.5f, 2.0f, 3.0f, 4.0f
    };
    uint32_t bpm_milli = 120000U;
    const uint8_t index = (uint8_t)(clamp_value(value, 0.0f, 12.0f) + 0.5f);

    if ((seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL)
            && (seq_runtime_is_external_tempo_valid() != 0U))
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
    }
    else
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
    }
    if (bpm_milli < 40000U)
        bpm_milli = 40000U;
    if (bpm_milli > 300000U)
        bpm_milli = 300000U;
    return beats[index] * 60000.0f / (float)bpm_milli;
}

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
                                                    float *out_command_value)
{
    if ((out_command_value == NULL) || (id >= PARAM_COUNT))
        return 0U;

    *out_command_value = canonical_value;
    switch (id)
    {
        case PARAM_MIX_DELAY_TIME:
        case PARAM_MIX_DELAY_TIME_R:
            *out_command_value = param_registry_global_delay_time_seconds(canonical_value);
            break;
        case PARAM_MODFX_RATE:
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET:
        case PARAM_MODFX_RATE_B:
        case PARAM_MODFX_DELAY_B:
        case PARAM_MODFX_DEPTH_B:
        case PARAM_MODFX_WIDTH:
            *out_command_value = param_registry_global_modfx_command(
                id, canonical_value, (uint8_t)(param_get(PARAM_MODFX_MODEL) + 0.5f));
            break;
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
    PARAM_AUDIO_FX_FILTER_POS,
    PARAM_AUDIO_FX_ORDER,
    PARAM_AUDIO_FX_MODE_A,
    PARAM_AUDIO_FX_MODE_B,
    PARAM_GROUP_FX_A_LEVEL,
    PARAM_GROUP_FX_B_LEVEL
};

uint8_t param_registry_is_audio_fx_param(param_id_t id)
{
    return audio_fx_runtime_is_param(id);
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
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 1U,
        .item = {{
            .parameter_id = (uint16_t)id,
            .scope = scope,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(value)
        }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

static uint8_t param_registry_apply_audio_fx_control(param_id_t id,
                                                      uint8_t track,
                                                      float value)
{
    if ((param_registry_is_audio_fx_param(id) == 0U)
            || (track >= BRICK_ENTITY_CAPACITY)
            || (track_runtime_get_effective_param_status(track, id)
                != TRACK_RUNTIME_PARAM_ALLOWED))
    {
        return 0U;
    }

    if ((id == PARAM_AUDIO_FX_FILTER_POS)
            && (param_registry_audio_fx_pre_filter_supported_control(track) == 0U))
        value = (float)AUDIO_FX_FILTER_POS_PRE;
    if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
        value = param_registry_audio_fx_clamp_p3(value);
    param_registry_audio_fx_set_control(track, id, value);
    (void)param_registry_control_value_get(track, id, &value);

    if (param_registry_submit_audio_value(
            id, track, value, LIVE_PARAMETER_EVENT_SCOPE_TRACK) == 0U)
        return 0U;
    if ((id == PARAM_AUDIO_FX_MODEL) || (id == PARAM_AUDIO_FX_B_MODEL))
        mod_destination_catalog_invalidate_track(track);
    return 1U;
}

uint8_t param_registry_project_track_effective_mute(
    uint8_t track, uint8_t effective_muted)
{
    const float value = (effective_muted != 0U) ? 1.0f : 0.0f;
    return param_registry_submit_audio_value(
        PARAM_MIX_MUTE, track, value, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
}
