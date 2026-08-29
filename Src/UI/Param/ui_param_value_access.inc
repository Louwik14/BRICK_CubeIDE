static uint8_t ui_param_is_track_scoped(param_id_t param)
{
    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    return ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint8_t ui_param_track_accepts_relative_param(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_LANE_CAPACITY) || (param >= PARAM_COUNT))
    {
        return 0U;
    }
    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return ui_param_seq_runtime_track_is_valid(track);
    }
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE))
    {
        return 0U;
    }

    if (track_runtime_get_effective_param_status(track, param) != TRACK_RUNTIME_PARAM_ALLOWED)
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV))
    {
        uint8_t set_id = 0U;
        seq_param_slot_t param_slot = 0U;
        return ui_param_resolve_seq_slot(track, param, &set_id, &param_slot);
    }

    return 1U;
}

static uint8_t ui_param_resolve_edit_bounds(param_id_t param, uint8_t track, float *out_min, float *out_max)
{
    if ((param >= PARAM_COUNT) || (out_min == 0) || (out_max == 0))
    {
        return 0U;
    }

    const param_desc_t *desc = &param_registry[param];
    *out_min = desc->min;
    *out_max = desc->max;
    if (((param == PARAM_AUDIO_FX_P3) || (param == PARAM_AUDIO_FX_B_P3))
            && ((uint8_t)(ui_param_get_active_track_value(
                    (param == PARAM_AUDIO_FX_B_P3) ? PARAM_AUDIO_FX_B_MODEL
                                                   : PARAM_AUDIO_FX_MODEL,
                    track) + 0.5f)
                == AUDIO_FX_MODEL_DRIVE))
    {
        *out_min = 0.0f;
        *out_max = 127.0f;
        return 1U;
    }
    if ((param == PARAM_WAVE_OSC1_TABLE) || (param == PARAM_WAVE_OSC2_TABLE))
    {
        uint16_t logical[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
        const uint16_t count=project_control_list_wavetables(logical,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS);
        if(count==0U)
        {
            return 0U;
        }
        *out_min = (float)logical[0];
        *out_max = (float)logical[count-1U];
        return 1U;
    }
    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    if (note_fx_state_param_map(param, &note_fx_slot, &note_fx_param) != 0U)
    {
        (void)note_fx_slot;
        float model_value = 0.0f;
        note_fx_param_schema_t schema;
        const param_id_t model_id = (param_id_t)(param
            + (NOTE_FX_PARAM_COUNT - 1U - note_fx_param));
        if ((note_fx_state_get_param(track, model_id, &model_value) != 0U)
                && (note_fx_state_get_param_schema((uint8_t)model_value,
                                                    note_fx_param,
                                                    &schema) != 0U))
        {
            *out_min = (float)schema.min;
            *out_max = (float)schema.max;
            if (((uint8_t)model_value == NOTE_FX_MODEL_EUCLID)
                    && (note_fx_param == 1U))
            {
                float length = 0.0f;
                const param_id_t length_id = (param_id_t)(param - 1U);
                if (note_fx_state_get_param(track, length_id, &length) != 0U)
                    *out_max = length;
            }
        }
        return 1U;
    }
    if (param == PARAM_CFG_TRACK)
    {
        *out_max = (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U);
    }
    else if (param == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(track);
        const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
        *out_max = (type_count > 0U) ? (float)(type_count - 1U) : 0.0f;
    }
    else if (param == PARAM_CFG_POLY_VOICES)
    {
        if ((ui_get_track_family(track) == UI_TRACK_FAMILY_SAMPLER)
                && (ui_get_track_type(track) == UI_TRACK_TYPE_MULTI))
        {
            *out_min = 1.0f;
            *out_max = (float)BRICK6_SAMPLER_MULTI_MAX_VOICES;
        }
        else
        {
            *out_max = (float)SYNTH_POLYPHONY_MAX_VOICES;
        }
    }
    else if (param == PARAM_MOD_MATRIX_DEST)
    {
        const uint16_t count = mod_lfo_v1_dest_count(track);
        *out_max = (count > 0U) ? (float)(count - 1U) : 0.0f;
    }
    else if ((param == PARAM_SAMPLER_SAMPLE)
             && (ui_get_track_family(track) == UI_TRACK_FAMILY_SAMPLER)
             && (ui_get_track_type(track) == UI_TRACK_TYPE_MULTI))
    {
        *out_min = 0.0f;
        *out_max = (float)multi_sample_pool_get_instrument_count();
    }
    else if ((param == PARAM_SAMPLER_SAMPLE)
             && (ui_get_track_family(track) == UI_TRACK_FAMILY_SAMPLER))
    {
        const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
        *out_min = 0.0f;
        *out_max = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    }

    return 1U;
}

static uint8_t ui_param_resolve_effective_edit_track(param_id_t param, uint8_t active_track)
{
    ui_page_template_play_context_t play_context;
    if (ui_param_resolve_play_context(param, active_track, &play_context) != 0U)
    {
        return play_context.target_track;
    }
    if (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
    {
        brick_entity_id_t owner = active_track;
        if (entity_topology_mod_owner(active_track, &owner) != 0U)
        {
            return owner;
        }
    }
    return active_track;
}

static uint8_t ui_param_resolve_play_context(param_id_t param,
                                             uint8_t active_track,
                                             ui_page_template_play_context_t *out_context)
{
    return ui_page_template_play_resolve_context(param, active_track, out_context);
}

static float ui_param_get_active_track_value(param_id_t param, uint8_t active_track)
{
    active_track = ui_param_resolve_effective_edit_track(param, active_track);

    float value = 0.0f;
    if (ui_param_control_value_get(param, active_track, &value) != 0U)
    {
        return value;
    }
    if (ui_param_get_seq_runtime_track_value(param, active_track, &value) != 0U)
    {
        return value;
    }
    if (ui_param_is_track_scoped(param) == 0U)
    {
        return param_get(param);
    }

    if (ui_param_get_track_edit_value(param, active_track, &value) != 0U)
    {
        return value;
    }

    return param_store_get_active(param);
}

float ui_param_get_active_track_display_value(param_id_t param, uint8_t active_track)
{
    const uint8_t display_track = ui_param_resolve_effective_edit_track(param, active_track);
    ui_param_live_rec_ctx_t live_rec_ctx;
    if (ui_param_live_rec_resolve_context(param, display_track, &live_rec_ctx) != 0U)
    {
        seq_plock_entry_t entry;
        if (ui_param_step_value_find(live_rec_ctx.track,
                                     live_rec_ctx.step,
                                     param,
                                     live_rec_ctx.set_id,
                                     live_rec_ctx.param_slot,
                                     &entry) != 0U)
        {
            return seq_param_iface_decode_param_value(param, entry.value16);
        }
    }

    return ui_param_get_active_track_value(param, active_track);
}

static uint8_t ui_param_get_track_edit_value(param_id_t param, uint8_t track, float *out_value)
{
    if (ui_param_control_value_get(param, track, out_value) != 0U)
    {
        return 1U;
    }
    if (ui_param_get_seq_runtime_track_value(param, track, out_value) != 0U)
    {
        return 1U;
    }
    return param_registry_get_track_value(param, track, out_value);
}

static uint8_t ui_param_is_relative_multi_track_candidate(param_id_t param, uint8_t active_track)
{
    if ((param >= PARAM_COUNT)
            || (active_track >= SEQ_LANE_CAPACITY)
            || (ui_param_is_track_scoped(param) == 0U))
    {
        return 0U;
    }

    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        return ui_param_track_accepts_relative_param(active_track, param);
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE))
    {
        return 0U;
    }

    return ui_param_track_accepts_relative_param(active_track, param);
}

static uint8_t ui_param_resolve_seq_slot(uint8_t track,
                                         param_id_t param,
                                         uint8_t *out_set_id,
                                         seq_param_slot_t *out_param_slot)
{
    if ((out_set_id == 0) || (out_param_slot == 0) || (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    uint8_t set_id = 0U;
    if ((param >= PARAM_FM_OPERATOR_FIRST) && (param <= PARAM_FM_OPERATOR_LAST))
    {
        set_id = (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR;
    }
    else
    switch (rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_CFG:
            return 0U;
        case TRACK_RUNTIME_PARAM_DOMAIN_ENV:
            set_id = (uint8_t)SEQ_PLOCK_SET_ENV;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
        {
            uint8_t voice = 0U;
            seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
            if ((seq_model_track_can_store_play(track) == 0U)
                    || (seq_model_play_resolve_param(param, &voice, &field) == 0U))
            {
                return 0U;
            }
            *out_set_id = (uint8_t)SEQ_LIVE_REC_PARAM_SET_PLAY;
            *out_param_slot = 0U;
            return 1U;
        }
        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
            set_id = (uint8_t)SEQ_PLOCK_SET_MOD;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX:
            set_id = (uint8_t)SEQ_PLOCK_SET_MIDI_FX;
            break;
        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
            break;
        default:
            return 0U;
    }

    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param, &param_slot) == 0U)
    {
        return 0U;
    }

    *out_set_id = set_id;
    *out_param_slot = param_slot;
    return 1U;
}

static uint8_t ui_param_live_rec_resolve_context(param_id_t param,
                                                 uint8_t active_track,
                                                 ui_param_live_rec_ctx_t *out_ctx)
{
    if (out_ctx == 0)
    {
        return 0U;
    }

    const seq_track_id_t track = active_track;
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(track, param, &set_id, &param_slot) == 0U)
    {
        return 0U;
    }

    seq_step_id_t step = 0U;
    if (seq_runtime_live_rec_param_resolve_write_step(track,
                                                      set_id,
                                                      param_slot,
                                                      &step) == 0U)
    {
        return 0U;
    }

    out_ctx->track = track;
    out_ctx->step = step;
    out_ctx->set_id = set_id;
    out_ctx->param_slot = param_slot;
    return 1U;
}

static uint8_t ui_param_set_track_value(uint8_t encoder,
                                        param_id_t param,
                                        float value,
                                        uint8_t track,
                                        uint8_t update_active_mirror)
{
    if (live_parameter_is_audio_owned(param) != 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        float edit_min = desc->min;
        float edit_max = desc->max;
        if (ui_param_is_track_scoped(param) != 0U)
        {
            (void)ui_param_resolve_edit_bounds(param, track, &edit_min, &edit_max);
        }
        const float clamped = ui_param_clamp(value, edit_min, edit_max);
        return ui_param_control_value_set(param,
                                          track,
                                          clamped,
                                          update_active_mirror);
    }

    if (ui_param_is_seq_runtime_track_param(param) != 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        const float clamped = ui_param_clamp(value, desc->min, desc->max);
        float current_value = 0.0f;
        if (ui_param_get_seq_runtime_track_value(param, track, &current_value) == 0U)
        {
            return 0U;
        }

        if (ui_param_value_is_same(current_value, clamped) != 0U)
        {
            if (update_active_mirror != 0U)
            {
                param_store_set_active(param, clamped);
            }
            return 1U;
        }

        if (ui_param_apply_seq_runtime_track_value(param, track, clamped) == 0U)
        {
            return 0U;
        }

        if (update_active_mirror != 0U)
        {
            param_store_set_active(param, clamped);
        }
        return 1U;
    }


    if (ui_param_is_track_scoped(param) == 0U)
    {
        const param_desc_t *const desc = &param_registry[param];
        const float clamped = ui_param_clamp(value, desc->min, desc->max);
        const float before = param_get(param);
        if (ui_param_value_is_same(before, clamped) != 0U)
        {
            return 1U;
        }

        param_set(param, clamped);
        return 1U;
    }

    float edit_min = 0.0f;
    float edit_max = 0.0f;
    if (ui_param_resolve_edit_bounds(param, track, &edit_min, &edit_max) == 0U)
        return 0U;
    const float clamped = ui_param_clamp(value, edit_min, edit_max);
    float current_value = 0.0f;

    if ((param_registry_get_track_value(param, track, &current_value) != 0U)
            && (ui_param_value_is_same(current_value, clamped) != 0U))
    {
        /* Query check before command: avoid redundant apply when the effective value is unchanged. */
        if (update_active_mirror != 0U)
        {
            param_store_set_active(param, clamped);
        }
        return 1U;
    }

    const param_registry_track_edit_cmd_t edit_cmd = {
        .id = param,
        .track = track,
        .value = clamped
    };
    if (param_registry_apply_track_edit(&edit_cmd) == 0U)
    {
        return 0U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(track, param, &set_id, &param_slot) != 0U)
    {
        const seq_value16_t encoded = seq_param_iface_encode_param_value(param, clamped);
        const seq_param_iface_base_commit_cmd_t cmd = {
            .source = SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT,
            .authoritative_apply_done = 1U,
            .target_track = track,
            .set_id = set_id,
            .param_slot = param_slot,
            .value16 = encoded
        };
        (void)seq_param_iface_commit_base_after_authoritative_apply(&cmd);
    }

    /* Track-scoped contract: active[] mirrors the UI edit context; runtime authority is apply_track_value(track,...). */
    if (update_active_mirror != 0U)
    {
        param_store_set_active(param, clamped);
    }
    return 1U;
}

