void ui_param_publish_encoder_binding(uint8_t active_track, uint8_t shift_down)
{
    encoder_binding_snapshot_t snapshot = { 0 };

    for (uint8_t encoder = 0U; encoder < ENCODER_BINDING_ENCODER_COUNT; ++encoder)
    {
        param_id_t parameter = PARAM_COUNT;
        uint8_t scope = LIVE_PARAMETER_EVENT_SCOPE_GLOBAL;
        uint8_t track = 0U;
        const uint8_t slot = 0xFFU;
        encoder_binding_route_t route = ENCODER_BINDING_ROUTE_UI;
        uint8_t valid = 0U;
        const uint8_t track_modifier_down = (ui_is_track_modifier_held() != 0U) ? 1U : 0U;

        if ((g_ui_param.valid != 0U) && (g_ui_param.bank.params[encoder] < PARAM_COUNT))
        {
            parameter = g_ui_param.bank.params[encoder];
            valid = 1U;
            if (ui_param_is_track_scoped(parameter) != 0U)
            {
                scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK;
                track = ui_param_resolve_effective_edit_track(parameter, active_track);
            }
            if (live_parameter_is_audio_owned(parameter) != 0U)
            {
                route = ENCODER_BINDING_ROUTE_AUDIO;
            }
        }

        snapshot.entry[encoder] = encoder_binding_pack((uint16_t)parameter,
                                                       scope,
                                                       track,
                                                       slot,
                                                       shift_down,
                                                       route,
                                                       track_modifier_down,
                                                       valid);
    }

    encoders_set_binding_snapshot(&snapshot);
}

uint8_t ui_param_get_audio_owned_command_value(param_id_t param,
                                               uint8_t track,
                                               float *out_value)
{
    return ui_param_control_value_get(param, track, out_value);
}

uint8_t ui_param_accept_audio_owned_command(param_id_t param,
                                            uint8_t scope,
                                            uint8_t track,
                                            float value)
{
    const uint8_t control_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) ? track : 0U;
    const uint8_t update_global_mirror =
        (scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL) ? 1U : 0U;
    return ui_param_control_value_set(param,
                                      control_track,
                                      value,
                                      update_global_mirror);
}

static uint8_t ui_param_bank_is_same(const ui_param_bank_t *bank)
{
    if ((bank == 0) || (g_ui_param.valid == 0U))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (g_ui_param.bank.params[i] != bank->params[i])
        {
            return 0U;
        }
    }

    return 1U;
}

void ui_param_clear_value_flash(void)
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        g_ui_param_value_flash[i].active = 0U;
        g_ui_param_value_flash[i].param = PARAM_COUNT;
        g_ui_param_value_flash[i].track = 0U;
        g_ui_param_value_flash[i].value = 0.0f;
        g_ui_param_value_flash[i].kind = (uint8_t)UI_PARAM_VALUE_FLASH_DIRECT;
        g_ui_param_value_flash[i].until_ms = 0U;
    }
    g_ui_param_tweak_param = PARAM_COUNT;
    g_ui_param_tweak_slot = 0xFFU;
    g_ui_param_tweak_until_ms = 0U;
}

void ui_param_note_user_tweak(uint8_t slot, param_id_t param)
{
    if ((slot >= 4U) || (param >= PARAM_COUNT)) return;
    g_ui_param_tweak_slot = slot;
    g_ui_param_tweak_param = param;
    g_ui_param_tweak_until_ms = HAL_GetTick() + UI_PARAM_VALUE_FLASH_DURATION_MS;
}

uint8_t ui_param_is_user_tweak_active(uint8_t slot, param_id_t param)
{
    if ((slot != g_ui_param_tweak_slot) || (param != g_ui_param_tweak_param)
            || ((int32_t)(g_ui_param_tweak_until_ms - HAL_GetTick()) <= 0))
    {
        return 0U;
    }
    return 1U;
}

void ui_param_note_user_value_flash(uint8_t slot,
                                    param_id_t param,
                                    uint8_t track,
                                    float value,
                                    ui_param_value_flash_kind_t kind)
{
    if ((slot >= 4U) || (param >= PARAM_COUNT) || (track >= BRICK_ENTITY_CAPACITY))
    {
        return;
    }

    g_ui_param_value_flash[slot].param = param;
    g_ui_param_value_flash[slot].track = track;
    g_ui_param_value_flash[slot].value = value;
    g_ui_param_value_flash[slot].kind = (uint8_t)kind;
    g_ui_param_value_flash[slot].until_ms = HAL_GetTick() + UI_PARAM_VALUE_FLASH_DURATION_MS;
    g_ui_param_value_flash[slot].active = 1U;
}

uint8_t ui_param_get_slot_value_flash(uint8_t slot,
                                      param_id_t param,
                                      uint8_t track,
                                      float *out_value,
                                      ui_param_value_flash_kind_t *out_kind)
{
    if ((slot >= 4U) || (param >= PARAM_COUNT) || (out_value == 0))
    {
        return 0U;
    }

    ui_param_value_flash_slot_t *const flash = &g_ui_param_value_flash[slot];
    if ((flash->active == 0U)
            || (flash->param != param)
            || ((int32_t)(flash->until_ms - HAL_GetTick()) <= 0))
    {
        flash->active = 0U;
        return 0U;
    }
    if (flash->track != track)
    {
        return 0U;
    }

    *out_value = flash->value;
    if (out_kind != 0)
    {
        *out_kind = (ui_param_value_flash_kind_t)flash->kind;
    }
    return 1U;
}


/**
 * @brief Point d'entrée ui_param_clamp.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_clamp.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param min Paramètre d'entrée de l'API.
 * @param max Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
