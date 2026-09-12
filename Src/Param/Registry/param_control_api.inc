uint8_t param_registry_is_lfo_param(param_id_t id)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    return param_lfo_map(id, &lfo_index, &lfo_param);
}

uint8_t param_registry_is_plockable(param_id_t id)
{
    if (id >= PARAM_COUNT) return 0U;
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
        return 1U;

    switch (id)
    {
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_AUDIO_FX_MODEL:
        case PARAM_AUDIO_FX_B_MODEL:
        case PARAM_LOOPER_STRETCH:
        case PARAM_LOOPER_PITCH:
        case PARAM_LOOPER_GRAIN:
        case PARAM_FM_TRANSPOSE:
        case PARAM_FM_PITCH_R1:
        case PARAM_FM_PITCH_R2:
        case PARAM_FM_PITCH_R3:
        case PARAM_FM_PITCH_R4:
        case PARAM_FM_PITCH_L1:
        case PARAM_FM_PITCH_L2:
        case PARAM_FM_PITCH_L3:
        case PARAM_FM_PITCH_L4:
            return 0U;
        default:
            break;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        return (uint8_t)((id == PARAM_MIX_LEVEL) || (id == PARAM_MIX_PAN)
            || (id == PARAM_MIX_SEND1) || (id == PARAM_MIX_SEND2)
            || (id == PARAM_MIX_SEND3));
    return (uint8_t)((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX));
}

uint8_t param_registry_resolve_track_param(
    uint8_t track, param_id_t id,
    param_registry_resolved_track_param_t *out_param)
{
    if (out_param == NULL) return 0U;
    *out_param = (param_registry_resolved_track_param_t){
        .id = id, .applicable = 0U, .plockable = 0U, .label = NULL
    };
    if ((track >= BRICK_ENTITY_CAPACITY) || (id >= PARAM_COUNT)
            || (param_id_is_reserved(id) != 0U)) return 0U;

    out_param->plockable = param_registry_is_plockable(id);
    out_param->label = param_registry[id].name;
    const track_runtime_param_status_t status =
        track_runtime_get_effective_param_status(track, id);
    if ((status != TRACK_RUNTIME_PARAM_ALLOWED)
            && (status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) return 1U;
    if (param_registry_track_temp_is_applicable(id, track) == 0U) return 1U;

    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        static const char *const labels[MOD_LFO_COUNT_PER_TRACK]
            [MOD_LFO_PARAM_COUNT] = {
            { "LFO1 RATE", "LFO1 SHAPE", "LFO1 TRIG", "LFO1 PHASE" },
            { "LFO2 RATE", "LFO2 SHAPE", "LFO2 TRIG", "LFO2 PHASE" },
            { "LFO3 RATE", "LFO3 SHAPE", "LFO3 TRIG", "LFO3 PHASE" }
        };
        out_param->label = labels[lfo_index][lfo_param];
        out_param->applicable = 1U;
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_COUNT;
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
        set_id = (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        set_id = (uint8_t)SEQ_PLOCK_SET_ENV;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
        set_id = (uint8_t)SEQ_PLOCK_SET_AUDIO_FX;
    if ((set_id >= (uint8_t)SEQ_PLOCK_SET_COUNT)
            || (seq_param_iface_param_is_supported(track, set_id, id) == 0U))
        return 1U;

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
    {
        uint8_t fx_slot = 0U;
        uint8_t fx_param = 0U;
        if (audio_fx_param_catalog_param_info(
                id, &fx_slot, &fx_param) != 0U)
        {
            float model = 0.0f;
            if ((audio_fx_control_state_get_param(track,
                    (fx_slot != 0U) ? PARAM_AUDIO_FX_B_MODEL
                                    : PARAM_AUDIO_FX_MODEL, &model) == 0U)
                    || (audio_fx_param_catalog_resolve(
                        (uint8_t)(model + 0.5f), fx_param,
                        &out_param->label) == 0U)) return 1U;
        }
    }
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        if ((id >= PARAM_DRUM_MD_P1) && (id <= PARAM_DRUM_MD_P8))
        {
            float model = 0.0f;
            if (param_registry_get_track_value(
                    PARAM_DRUM_MD_MODEL, track, &model) == 0U) return 1U;
            const md_model_profile_t *const profile =
                md_model_profile_get(md_model_validate(model));
            const uint8_t slot = (uint8_t)(id - PARAM_DRUM_MD_P1);
            if ((slot >= profile->slot_count)
                    || (profile->slot_labels[slot] == NULL)) return 1U;
            out_param->label = profile->slot_labels[slot];
        }
        else
        {
            uint8_t dynamic_slot = 0U;
            uint8_t dynamic_param = 0U;
            if (param_stack_dynamic_param_info(
                    id, &dynamic_slot, &dynamic_param) != 0U)
            {
                if (param_stack_label_for_track_param(
                        track, id, &out_param->label) == 0U) return 1U;
            }
            else
            {
                const char *prism_label = NULL;
                if (param_prism_label_for_track_param(
                        track, id, &prism_label) != 0U)
                {
                    if ((prism_label == NULL)
                            || (strcmp(prism_label, "-") == 0)) return 1U;
                    out_param->label = prism_label;
                }
            }
        }
    }

    if ((out_param->label != NULL) && (out_param->label[0] != '\0'))
        out_param->applicable = 1U;
    return 1U;
}

uint8_t param_registry_projected_track_param_is_applicable(
    param_id_t id, track_family_t family, track_runtime_type_t type,
    uint8_t is_group_master, uint8_t is_group_child)
{
    if ((id >= PARAM_COUNT) || (family == TRACK_FAMILY_OFF)
            || (param_registry_is_plockable(id) == 0U)) return 0U;
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD) return 1U;
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        return 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        if ((family == TRACK_FAMILY_MIDI)
                || (id == PARAM_MIDI_PROGRAM)
                || ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)))
            return 0U;
        const uint8_t set_id = ((id >= PARAM_FM_OPERATOR_FIRST)
                && (id <= PARAM_FM_OPERATOR_LAST))
            ? (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR
            : (uint8_t)SEQ_PLOCK_SET_TONE;
        seq_param_slot_t slot = 0U;
        return seq_param_iface_param_to_slot_for_type((uint8_t)type,
            is_group_master, set_id, id, &slot);
    }
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
    {
        const uint8_t group_level = (uint8_t)(
            (id == PARAM_GROUP_FX_A_LEVEL)
            || (id == PARAM_GROUP_FX_B_LEVEL));
        return (is_group_child != 0U) ? group_level
                                     : (uint8_t)(group_level == 0U);
    }
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        return (uint8_t)(family != TRACK_FAMILY_MIDI);
    if (is_group_master != 0U)
        return (uint8_t)((id == PARAM_FILTER_MORPH)
            || (id == PARAM_FILTER_CUTOFF) || (id == PARAM_FILTER_RESONANCE)
            || (id == PARAM_FILTER_KEYTRK) || (id == PARAM_ENV3_ATTACK)
            || (id == PARAM_ENV3_DECAY) || (id == PARAM_ENV3_SUSTAIN)
            || (id == PARAM_ENV3_RELEASE));
    if (rule.resource == TRACK_RUNTIME_RESOURCE_FILTER)
        return (uint8_t)((family == TRACK_FAMILY_SYNTH)
            || (family == TRACK_FAMILY_SAMPLER)
            || (family == TRACK_FAMILY_DRUM)
            || (family == TRACK_FAMILY_EXTERNAL));
    return 1U;
}

uint8_t param_registry_track_value_is_audio_command(param_id_t id,
                                                    uint8_t track)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const uint8_t midi_tone = (uint8_t)(
        (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (param_backend_track_supports_midi_tone_ctx(
                track_runtime_get_ctx(track)) != 0U));
    return (uint8_t)(
        (id == PARAM_EXT_GATE)
        || (id == PARAM_CFG_POLY_SPREAD)
        || (param_registry_is_lfo_param(id) != 0U)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_GLOBAL)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (midi_tone == 0U))
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX));
}

uint8_t param_registry_track_temp_is_applicable(param_id_t id, uint8_t track)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY)) return 0U;
    if (param_registry_is_lfo_param(id) != 0U) return 1U;
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        return 0U;
    return param_registry_track_value_is_audio_command(id, track);
}

uint8_t param_registry_temp_is_clearable(param_id_t id)
{
    return (uint8_t)((id < PARAM_COUNT)
        && ((param_registry_is_lfo_param(id) != 0U)
            || ((id >= PARAM_ENV3_ATTACK) && (id <= PARAM_ENV3_RELEASE))));
}

uint8_t param_registry_install_prepared_track_control_target(
    const param_registry_prepared_track_target_t *prepared)
{
    if ((prepared == NULL)
            || (prepared->id >= PARAM_COUNT)
            || (prepared->track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    const param_id_t id = prepared->id;
    const uint8_t track = prepared->track;
    const float value = prepared->canonical_value;
    switch (prepared->owner_kind)
    {
        case PARAM_CONTROL_OWNER_AUDIO_ONLY: return 1U;
        case PARAM_CONTROL_OWNER_TONE:
            return tone_program_control_set(track, id, value);
        case PARAM_CONTROL_OWNER_FM:
            return fm_control_state_set_public_param(track, id, value);
        case PARAM_CONTROL_OWNER_MUTE:
            return track_mute_install(track, (value >= 0.5f) ? 1U : 0U);
        case PARAM_CONTROL_OWNER_AUDIO_FX:
        {
            const uint8_t installed =
                audio_fx_control_install_prepared_param(track, id, value);
            if ((installed != 0U) && ((id == PARAM_AUDIO_FX_MODEL)
                    || (id == PARAM_AUDIO_FX_B_MODEL)))
                mod_destination_catalog_invalidate_track(track);
            return installed;
        }
        case PARAM_CONTROL_OWNER_FILTER:
            return param_filter_control_set(track, id, value);
        case PARAM_CONTROL_OWNER_VCA:
            return vca_control_state_set_param(track, id, value);
        case PARAM_CONTROL_OWNER_ENV3:
            return mod_env3_control_set_param(track, id, value);
        case PARAM_CONTROL_OWNER_MIX:
            return mixer_control_state_set_param(track, id, value);
        case PARAM_CONTROL_OWNER_POLYPHONY:
            return polyphony_control_set_spread(track, value);
        case PARAM_CONTROL_OWNER_LFO:
        {
            uint8_t lfo_index = 0U;
            mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
            return (uint8_t)((param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
                && (mod_lfo_v1_install_prepared_track_param(
                    track, lfo_index, lfo_param, value) != 0U));
        }
        default: return 0U;
    }
}

/* Command surface: track-aware apply and post-commit routing. */
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    if (id == PARAM_MIX_MUTE)
    {
        param_registry_prepared_value_t prepared;
        value = param_value_policy_canonicalize(id, track, value);
        if (param_registry_prepare_value(id, value, &prepared) == 0U)
            return 0U;
        return track_mute_set(track, (prepared.value >= 0.5f) ? 1U : 0U);
    }

    if (param_registry_track_value_is_audio_command(id, track) != 0U)
    {
        audio_fx_control_prepare_context_t audio_fx_context = {0};
        param_registry_prepared_track_target_t target;
        if (param_registry_prepare_track_control_target(id, track, value,
                &audio_fx_context, &target) == 0U)
            return 0U;
        if (param_registry_submit_audio_value(
                id, track, target.canonical_value,
                LIVE_PARAMETER_EVENT_SCOPE_TRACK) == 0U)
            return 0U;
        const uint8_t installed =
            param_registry_install_prepared_track_control_target(&target);
        if (installed == 0U) Error_Handler();
        return installed;
    }

    param_registry_prepared_value_t prepared;
    value = param_value_policy_canonicalize(id, track, value);
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
        return 0U;
    const float clamped = prepared.value;

    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    if (note_fx_state_param_map(id, &note_fx_slot, &note_fx_param) != 0U)
    {
        if (track_runtime_is_ui_ensemble_available(track, TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX) == 0U)
        {
            return 0U;
        }
        /* NoteFx owns the model-aware schema.  The static catalog only carries
         * the generic ARP/p-lock bounds and cannot clamp EUCLID LENGTH/PULSE. */
        if (note_fx_state_set_param(track, id, value) == 0U)
        {
            return 0U;
        }
        return note_fx_pipeline_configure_track(track);
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            uint8_t owner = 0U;
            float canonical = 0.0f;
            if ((mod_lfo_v1_prepare_track_param(track, lfo_index, lfo_param,
                    clamped, &owner, &canonical) == 0U)
                    || (param_registry_submit_audio_value(
                        id, owner, canonical,
                        LIVE_PARAMETER_EVENT_SCOPE_TRACK) == 0U))
                return 0U;
            const uint8_t installed = mod_lfo_v1_install_prepared_track_param(
                owner, lfo_index, lfo_param, canonical);
            if (installed == 0U) Error_Handler();
            return installed;
        }
    }

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t ok = param_apply_filter_track_value(id, track, clamped);
        return ok;
    }

    const uint8_t ok = param_apply_non_filter_track_value(id, track, clamped);
    return ok;
}

/**
 * @brief Point d'entrée param_registry_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_registry_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_registry_init(void)
{
    /* Registry is metadata and routing only; values remain in their owners. */
    param_global_control_init();
    tone_program_control_init();
    track_sound_state_init();
    fm_control_state_init();
    vca_control_state_init();
    mixer_control_state_init();
    mod_env3_control_init();
    track_mute_init();
    polyphony_control_init();
    audio_fx_control_state_init();
    param_macro_init();
    param_filter_init();
    mod_lfo_v1_init();
    note_fx_state_init();
}

/**
 * @brief Query des paramètres globaux.
 *
 * Rôle:
 * - Lire la valeur depuis son owner global.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Query surface: explicit owner dispatch, with no generic fallback storage. */
uint8_t param_registry_query_global(param_id_t id, float *out_value)
{
    if ((out_value == NULL) || (id >= PARAM_COUNT)
            || (param_id_is_reserved(id) != 0U)) return 0U;
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_GLOBAL)
            || (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) return 0U;
    return param_global_control_get(id, out_value);
}

uint8_t param_registry_install_prepared_global_control_target(
    param_id_t id, float canonical_value)
{
    return param_global_control_set(id, canonical_value);
}

/**
 * @brief Commit des paramètres globaux.
 *
 * Rôle:
 * - Préparer, publier et installer la valeur chez son owner.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Durable command surface: prepare, publish AUDIO, then deterministically
 * install the exact prepared value in its CONTROL owner. */
uint8_t param_registry_commit_global(param_id_t id, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return 0U;

    param_registry_prepared_value_t prepared;
    if (!isfinite(value)
            || (param_registry_prepare_value(id, value, &prepared) == 0U))
        return 0U;
    const float canonical = prepared.value;

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_GLOBAL)
            || (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) return 0U;

    if (param_registry_track_value_is_audio_command(id, 0U) != 0U)
    {
        float model_value;
        if (id == PARAM_MODFX_MODEL)
            model_value = canonical;
        else if (param_registry_query_global(PARAM_MODFX_MODEL,
                     &model_value) == 0U)
            return 0U;
        float command_value = canonical;
        if ((param_registry_prepare_global_audio_command(
                id, canonical, (uint8_t)(model_value + 0.5f),
                &command_value) == 0U)
                || (param_registry_submit_audio_value(
                    id, 0U, command_value,
                    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL) == 0U))
        {
            return 0U;
        }
        const uint8_t installed = param_global_control_set(id, canonical);
        if (installed == 0U) Error_Handler();
        return installed;
    }

    if (param_global_control_set(id, canonical) == 0U) return 0U;
    return 1U;
}

uint8_t param_registry_prepare_track_control_target(
    param_id_t id, uint8_t track, float value,
    audio_fx_control_prepare_context_t *audio_fx_context,
    param_registry_prepared_track_target_t *out_target)
{
    if ((out_target == NULL) || (id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY) || (param_id_is_reserved(id) != 0U)
            || !isfinite(value)
            || (param_registry_track_value_is_audio_command(id, track) == 0U)
            || (track_runtime_get_effective_param_status(track, id)
                != TRACK_RUNTIME_PARAM_ALLOWED))
        return 0U;
    param_registry_prepared_value_t prepared;
    value = param_value_policy_canonicalize(id, track, value);
    if (!isfinite(value) || (param_registry_prepare_value(id, value, &prepared) == 0U))
        return 0U;
    value = prepared.value;
    param_control_owner_kind_t owner_kind = PARAM_CONTROL_OWNER_AUDIO_ONLY;
    if (param_registry_is_lfo_param(id) != 0U)
    {
        uint8_t lfo_index = 0U;
        uint8_t owner = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if ((param_lfo_map(id, &lfo_index, &lfo_param) == 0U)
                || (mod_lfo_v1_prepare_track_param(track, lfo_index, lfo_param,
                    value, &owner, &value) == 0U))
            return 0U;
        track = owner;
        owner_kind = PARAM_CONTROL_OWNER_LFO;
    }
    else if (id == PARAM_MIX_MUTE)
        owner_kind = PARAM_CONTROL_OWNER_MUTE;
    else if (param_registry_is_audio_fx_param(id) != 0U)
    {
        if ((audio_fx_context == NULL)
                || (audio_fx_control_prepare_param(track, id, value,
                    audio_fx_context, &value) == 0U))
            return 0U;
        owner_kind = PARAM_CONTROL_OWNER_AUDIO_FX;
    }
    else
    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        {
            float current = 0.0f;
            if (param_id_is_fm_public(id) != 0U)
            {
                if (fm_control_state_get_public_param(track, id, &current) == 0U)
                    return 0U;
                owner_kind = PARAM_CONTROL_OWNER_FM;
            }
            else
            {
                if (tone_program_control_get(track, id, &current) == 0U)
                    return 0U;
                owner_kind = PARAM_CONTROL_OWNER_TONE;
            }
        }
        else if (param_filter_is_param(id) != 0U)
            owner_kind = PARAM_CONTROL_OWNER_FILTER;
        else if (((id >= PARAM_VCA_ATTACK) && (id <= PARAM_VCA_RELEASE))
                || (id == PARAM_FILTER_MODE) || (id == PARAM_ENV_RETRIG_VCA))
            owner_kind = PARAM_CONTROL_OWNER_VCA;
        else if ((id >= PARAM_ENV3_ATTACK) && (id <= PARAM_ENV3_RELEASE))
            owner_kind = PARAM_CONTROL_OWNER_ENV3;
        else if ((id == PARAM_MIX_LEVEL) || (id == PARAM_MIX_PAN)
                || (id == PARAM_MIX_SEND1) || (id == PARAM_MIX_SEND2)
                || (id == PARAM_MIX_SEND3))
            owner_kind = PARAM_CONTROL_OWNER_MIX;
        else if (id == PARAM_CFG_POLY_SPREAD)
            owner_kind = PARAM_CONTROL_OWNER_POLYPHONY;
        else return 0U;
    }
    *out_target = (param_registry_prepared_track_target_t){
        .id=id, .track=track, .owner_kind=owner_kind, .canonical_value=value };
    return 1U;
}

/**
 * @brief Reset d'un paramètre global.
 *
 * Rôle:
 * - Réappliquer le default via le chemin de commit global.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Command surface: reset global canonical value to default. */
uint8_t param_registry_reset_global(param_id_t id)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return 0U;

    return param_registry_commit_global(id, param_registry[id].default_value);
}
