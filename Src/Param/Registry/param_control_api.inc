uint8_t param_registry_is_lfo_param(param_id_t id)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    return param_lfo_map(id, &lfo_index, &lfo_param);
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
        (live_parameter_is_audio_owned(id) != 0U)
        || (id == PARAM_CFG_POLY_SPREAD)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (midi_tone == 0U))
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
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
            return audio_fx_control_install_prepared_param(track, id, value);
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
        return param_registry_install_prepared_track_control_target(&target);
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
            return mod_lfo_v1_install_prepared_track_param(
                owner, lfo_index, lfo_param, canonical);
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
/* Command surface: commit to the resolved CONTROL owner, then publish AUDIO. */
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

    if ((live_parameter_is_audio_owned(id) != 0U)
            || (id == PARAM_MASTER_GAIN))
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
        return param_global_control_set(id, canonical);
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
    if (id == PARAM_MIX_MUTE)
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
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                && (live_parameter_is_audio_owned(id) != 0U))
        {
            float current = 0.0f;
            if ((id >= PARAM_FM_UI_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
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
        else if (((id >= PARAM_ENV3_ATTACK) && (id <= PARAM_ENV3_RELEASE))
                || (id == PARAM_ENV_RETRIG_MOD))
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
