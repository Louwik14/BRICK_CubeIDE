static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

extern const param_desc_t param_registry[PARAM_COUNT];

static const param_id_t g_param_registry_lfo_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
    { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
    { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
    { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
};

static uint8_t param_control_owner_get(uint8_t track, param_id_t id,
                                       float *out_value)
{
    if (param_filter_control_get(track, id, out_value) != 0U) return 1U;
    if (vca_control_state_get_param(track, id, out_value) != 0U) return 1U;
    if (mod_env3_control_get_param(track, id, out_value) != 0U) return 1U;
    if (mixer_control_state_get_param(track, id, out_value) != 0U) return 1U;
    if (id == PARAM_MIX_MUTE)
    {
        const int8_t muted = track_mute_get(track);
        if (muted < 0) return 0U;
        *out_value = (float)muted;
        return 1U;
    }
    if (id == PARAM_CFG_POLY_SPREAD)
        return polyphony_control_get_spread(track, out_value);
    return audio_fx_control_state_get_param(track, id, out_value);
}

static uint8_t param_lfo_map(param_id_t id, uint8_t *out_lfo_index, mod_lfo_param_t *out_lfo_param)
{
    if ((out_lfo_index == NULL) || (out_lfo_param == NULL))
    {
        return 0U;
    }

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        for (uint8_t param = 0U; param < (uint8_t)MOD_LFO_PARAM_COUNT; ++param)
        {
            if (g_param_registry_lfo_params[lfo][param] == id)
            {
                *out_lfo_index = lfo;
                *out_lfo_param = (mod_lfo_param_t)param;
                return 1U;
            }
        }
    }

    return 0U;
}

static uint8_t param_registry_set_track_tone_value(param_id_t id,
                                                    uint8_t track,
                                                    float value)
{
    float previous;
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT)
            || (tone_program_control_get(track, id, &previous) == 0U))
        return 0U;
    if (id == PARAM_DRUM_MD_MODEL)
    {
        const uint8_t raw_model = (uint8_t)(value + 0.5f);
        const uint8_t model = (raw_model < (uint8_t)MD_MODEL_COUNT)
            ? raw_model : (uint8_t)MD_MODEL_TRX_BD;
        value = (float)model;
        mod_destination_catalog_invalidate_track(track);
    }
    (void)tone_program_control_set(track, id, value);
    return 1U;
}

typedef struct param_track_exec_ctx_t
{
    uint8_t track;
    param_id_t id;
    float clamped;
    uint8_t rt_fast;
    track_runtime_param_rule_t rule;
    track_runtime_resolved_track_t resolved;
} param_track_exec_ctx_t;

static uint8_t param_track_exec_apply_tone_drum_range(const param_track_exec_ctx_t *ctx,
                                                      track_runtime_type_t type,
                                                      param_id_t first_id,
                                                      param_id_t last_id)
{
    if ((ctx == NULL)
        || (ctx->rt_fast != 0U)
        || (ctx->rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (ctx->resolved.descriptor.type != type)
        || (ctx->id < first_id)
        || (ctx->id > last_id))
    {
        return 2U;
    }

    if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
    {
        return 0U;
    }

    return param_backend_apply_track_value_control(
        ctx->track, ctx->id, ctx->clamped);
}

static uint8_t param_track_exec_ctx_build(param_track_exec_ctx_t *ctx,
                                          uint8_t track,
                                          param_id_t id,
                                          float clamped,
                                          track_runtime_param_rule_t rule,
                                          uint8_t rt_fast)
{
    if ((ctx == NULL) || (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->track = track;
    ctx->id = id;
    ctx->clamped = clamped;
    ctx->rt_fast = rt_fast;
    ctx->rule = rule;

    if (track_runtime_resolve_track(track, &ctx->resolved) == 0U)
    {
        return 0U;
    }

    if (ctx->resolved.descriptor.active == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t param_track_exec_authorize(const param_track_exec_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    if (ctx->rt_fast != 0U)
    {
        if ((ctx->rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                || (ctx->id == PARAM_MIDI_PROGRAM))
        {
            return 0U;
        }

        return 1U;
    }

    if ((ctx->rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || ((ctx->id != PARAM_MIDI_PROGRAM) && (param_backend_is_midi_cc_id(ctx->id) == 0U)))
    {
        return 1U;
    }

    return param_backend_track_supports_midi_tone_descriptor(&ctx->resolved.descriptor);
}

static uint8_t param_track_exec_apply_backend(const param_track_exec_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }
    if ((ctx->rt_fast == 0U) && (ctx->id == PARAM_MIDI_PROGRAM))
    {
        if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
        {
            return 0U;
        }
        (void)tone_program_control_set(ctx->track, ctx->id, ctx->clamped);
        seq_runtime_on_midi_program_live_change(ctx->track, ctx->clamped);
        return 1U;
    }

    if ((ctx->rt_fast == 0U) && (param_backend_is_midi_cc_id(ctx->id) != 0U))
    {
        if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
        {
            return 0U;
        }
        if (param_backend_send_midi_cc(ctx->track, ctx->id, ctx->clamped) == 0U)
        {
            return 0U;
        }
        (void)tone_program_control_set(ctx->track, ctx->id, ctx->clamped);
        return 1U;
    }

    {
        const uint8_t applied = param_track_exec_apply_tone_drum_range(ctx,
                                                                        TRACK_RUNTIME_TYPE_DRUM_MD,
                                                                        PARAM_DRUM_MD_MODEL,
                                                                        PARAM_DRUM_MD_P8);
        if (applied != 2U)
        {
            return applied;
        }
    }

    if (ctx->rt_fast != 0U)
        return 0U;
    const uint8_t applied = param_backend_apply_track_value_control(
        ctx->track, ctx->id, ctx->clamped);
    if ((applied != 0U) && (ctx->rt_fast == 0U))
        (void)tone_program_control_set(ctx->track, ctx->id, ctx->clamped);
    return applied;
}

static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_GLOBAL)
            && (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
    {
        if (rt_fast != 0U)
        {
            return 0U;
        }

        return param_registry_commit_global(id, clamped);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
    {
        return 0U;
    }

    param_track_exec_ctx_t ctx;
    if (param_track_exec_ctx_build(&ctx, track, id, clamped, rule, rt_fast) == 0U)
    {
        return 0U;
    }

    if (param_track_exec_authorize(&ctx) == 0U)
    {
        return 0U;
    }

    return param_track_exec_apply_backend(&ctx);
}

static uint8_t param_apply_non_filter_track_value(param_id_t id, uint8_t track, float clamped)
{
    return param_apply_non_filter_track_value_core(id, track, clamped, 0U);
}

static uint8_t param_apply_filter_track_value(param_id_t id, uint8_t track, float clamped)
{
    return param_filter_apply_value(id, track, clamped, 1U);
}

/* Query surface: pure value read, no mutation, no resync, no transition. */
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    if ((id >= PARAM_COUNT) || (out_value == NULL)
            || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    if (note_fx_state_param_map(id, &note_fx_slot, &note_fx_param) != 0U)
    {
        return note_fx_state_get_param(track, id, out_value);
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
            return mod_lfo_v1_get_track_param(track, lfo_index, lfo_param, out_value);
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.status != TRACK_RUNTIME_PARAM_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_GLOBAL)
            || (track >= SEQ_LANE_CAPACITY))
        return 0U;
    if (fm_control_state_get_public_param(track, id, out_value) != 0U)
        return 1U;
    if (param_control_owner_get(track, id, out_value) != 0U)
        return 1U;
    return tone_program_control_get(track, id, out_value);
}
/* Command surface: RT fast-path apply reserved for modulation callers. */
