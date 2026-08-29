uint8_t param_registry_is_lfo_param(param_id_t id)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    return param_lfo_map(id, &lfo_index, &lfo_param);
}

void param_registry_release_track_value_runtime_temp(param_id_t id, uint8_t track)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        (void)param_registry_publish_track_value_runtime_temp(
            id, track, 0.0f);
        return;
    }

}

void param_registry_clear_track_runtime_state(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    /* Only AUDIO runtime overrides are released; CONTROL values are untouched. */
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        param_registry_release_track_value_runtime_temp((param_id_t)raw_id, track);
    }
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
        || (id == PARAM_CFG_POLY_VOICES)
        || (id == PARAM_CFG_POLY_SPREAD)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (id != PARAM_FM_OPERATOR_SELECT)
            && (id != PARAM_LOOPER_ARM)
            && (midi_tone == 0U))
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
}

uint8_t param_registry_install_prepared_track_control_target(
    const param_registry_prepared_value_t *prepared,
    uint8_t track)
{
    if ((prepared == NULL)
            || (prepared->id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(prepared->id) != 0U)
            || (param_registry_track_value_is_audio_command(
                    prepared->id, track) == 0U))
    {
        return 0U;
    }

    const param_id_t id = prepared->id;
    const float value = param_value_policy_canonicalize(prepared->id, track,
                                                         prepared->value);
    if (id == PARAM_MIX_MUTE)
    {
        const float muted = (value >= 0.5f) ? 1.0f : 0.0f;
        param_registry_control_value_set(track, id, muted);
        return 1U;
    }
    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        if ((track >= BRICK_ENTITY_CAPACITY)
                || (track_runtime_get_effective_param_status(track, id)
                    != TRACK_RUNTIME_PARAM_ALLOWED))
        {
            return 0U;
        }
        float effective = value;
        if ((id == PARAM_AUDIO_FX_FILTER_POS)
                && (param_registry_audio_fx_pre_filter_supported_control(track) == 0U))
            effective = (float)AUDIO_FX_FILTER_POS_PRE;
        if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
            effective = param_registry_audio_fx_clamp_p3(effective);
        param_registry_audio_fx_set_control(track, id, effective);

        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (live_parameter_is_audio_owned(id) == 0U)
            && (param_registry_set_track_tone_value(id, track, value) == 0U))
    {
        return 0U;
    }
    param_registry_control_value_set(track, id, value);
    if ((id == PARAM_PRISM_OSC1_MODEL)
            || (id == PARAM_PRISM_OSC2_MODEL)
            || (id == PARAM_STACK_OSC1_MODEL)
            || (id == PARAM_STACK_OSC2_MODEL)
            || (id == PARAM_STACK_OSC3_MODEL))
    {
        mod_destination_catalog_invalidate_track(track);
    }
    return 1U;
}

uint8_t param_registry_install_prepared_global_control_target(
    const param_registry_prepared_value_t *prepared)
{
    if ((prepared == NULL) || (prepared->id >= PARAM_COUNT)
            || (param_id_is_reserved(prepared->id) != 0U)
            || ((live_parameter_is_audio_owned(prepared->id) == 0U)
                && (prepared->id != PARAM_MASTER_GAIN)))
        return 0U;
    param_store_set_active(prepared->id, prepared->value);
    return 1U;
}

/* Command surface: track-aware apply and post-commit routing. */
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    value = param_value_policy_canonicalize(id, track, value);
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = prepared.value;
    if (id == PARAM_MIX_MUTE)
        return track_mute_set(track, (clamped >= 0.5f) ? 1U : 0U);

    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        return param_registry_apply_audio_fx_control(id, track, clamped);
    }

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
        const uint8_t audio_command =
            param_registry_track_value_is_audio_command(id, track);
        if (audio_command != 0U)
        {
            if ((id == PARAM_CFG_POLY_VOICES)
                    && ((uint8_t)clamped
                        < control_music_output_count(track)))
                return 0U;
            if (param_registry_install_prepared_track_control_target(
                    &prepared, track) == 0U)
                return 0U;
            const uint8_t submitted = param_registry_submit_audio_value(
                id, track, clamped, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
            return submitted;
        }
    }

    if ((id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE))
    {
        const uint8_t ok = param_apply_cfg_track_value(id, track, clamped);
        return ok;
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            const uint8_t ok = mod_lfo_v1_set_track_param(track, lfo_index, lfo_param, clamped);
            return (uint8_t)((ok != 0U)
                && param_registry_submit_audio_value(
                    id, track, clamped, LIVE_PARAMETER_EVENT_SCOPE_TRACK));
        }
    }

    if ((id == PARAM_MOD_MATRIX_SLOT)
            || (id == PARAM_MOD_MATRIX_SOURCE)
            || (id == PARAM_MOD_MATRIX_DEST)
            || (id == PARAM_MOD_MATRIX_DEPTH))
    {
        const uint8_t ok = param_matrix_set_track_value(id, track, clamped);
        return ok;
    }

    if ((id == PARAM_MOD_MULTI_1_A)
            || (id == PARAM_MOD_MULTI_1_B)
            || (id == PARAM_MOD_MULTI_2_A)
            || (id == PARAM_MOD_MULTI_2_B)
            || (id == PARAM_MOD_SLEW_1_SOURCE)
            || (id == PARAM_MOD_SLEW_1_AMOUNT)
            || (id == PARAM_MOD_SLEW_2_SOURCE)
            || (id == PARAM_MOD_SLEW_2_AMOUNT))
    {
        const uint8_t ok = param_mod_operator_set_track_value(id, track, clamped);
        return ok;
    }

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t ok = param_apply_filter_track_value(id, track, clamped);
        return ok;
    }

    const uint8_t ok = param_apply_non_filter_track_value(id, track, clamped);
    return ok;
}

/* Command surface: UI edit command forwarded to the track-aware apply seam. */
uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd)
{
    if ((cmd == NULL) || (cmd->id >= PARAM_COUNT) || (cmd->track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    return param_registry_apply_track_value(cmd->id, cmd->track, cmd->value);
}

/* Post-commit notification: refresh the filter UI mirror for the active track. */
void param_registry_sync_filter_ui_for_active_track(void)
{
    /* Post-commit notification: keep UI mirror aligned with the active track filter context. */
    param_filter_sync_ui_for_active_track();
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
    /* Registry is static metadata; runtime values are in param_store. */
    param_registry_control_values_init();
    track_sound_state_init();
    param_macro_init();
    param_filter_init();
    mod_lfo_v1_init();
    note_fx_state_init();
}

/**
 * @brief Point d'entrée param_get.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_get.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Query surface: global canonical value read. */
float param_get(param_id_t id)
{
    return param_store_get_active(id);
}

/**
 * @brief Point d'entrée param_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_set.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Command surface: global canonical write. */
void param_set(param_id_t id, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return;

    const param_desc_t *desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    param_store_set_active(id, clamped);

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            return;
        }
    }

    if ((live_parameter_is_audio_owned(id) != 0U)
            || (id == PARAM_MASTER_GAIN))
    {
        float command_value = clamped;
        if ((param_registry_prepare_global_audio_command(
                id, clamped, &command_value) == 0U)
                || (param_registry_submit_audio_value(
                    id, 0U, command_value,
                    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL) == 0U))
        {
            return;
        }
        return;
    }

    if (desc->apply != NULL)
    {
        desc->apply(clamped);
    }
}

/**
 * @brief Point d'entrée param_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_reset.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Command surface: reset global canonical value to default. */
void param_reset(param_id_t id)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return;

    param_set(id, param_registry[id].default_value);
}
