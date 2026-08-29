static float ui_param_clamp(float v, float min, float max)
{
    if (v < min)
    {
        return min;
    }

    if (v > max)
    {
        return max;
    }

    return v;
}

static int8_t ui_param_signum(int16_t value)
{
    if (value > 0)
    {
        return 1;
    }

    if (value < 0)
    {
        return -1;
    }

    return 0;
}

static uint8_t ui_param_value_is_same(float a, float b)
{
    const float diff = a - b;
    return ((diff > -0.000001f) && (diff < 0.000001f)) ? 1U : 0U;
}

static void ui_param_reset_stepped_encoder_accum(void)
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        g_ui_param_stepped_encoder_accum[i] = 0;
        g_ui_param_stepped_encoder_key[i] = 0UL;
    }
}

static uint8_t ui_param_is_seq_runtime_track_param(param_id_t param)
{
    switch (param)
    {
        case PARAM_SEQ_LENGTH:
        case PARAM_SEQ_DIV:
        case PARAM_SEQ_QUANT:
        case PARAM_SEQ_SWING:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t ui_param_seq_runtime_track_is_valid(uint8_t track)
{
    return (uint8_t)(track < SEQ_LANE_CAPACITY);
}

static uint8_t ui_param_get_seq_runtime_track_value(param_id_t param, uint8_t track, float *out_value)
{
    if ((out_value == 0) || (ui_param_seq_runtime_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    uint8_t value = 0U;
    switch (param)
    {
        case PARAM_SEQ_DIV:
            if (seq_runtime_get_track_div(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = (float)seq_division_track_div_to_ui(value);
            return 1U;

        case PARAM_SEQ_LENGTH:
            *out_value = (float)seq_model_get_track_length(track);
            return 1U;

        case PARAM_SEQ_QUANT:
            if (seq_runtime_get_track_quant(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = (float)value;
            return 1U;

        case PARAM_SEQ_SWING:
            if (seq_runtime_get_track_swing(track, &value) == 0U)
            {
                return 0U;
            }
            *out_value = (float)value;
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_param_apply_seq_runtime_track_value(param_id_t param, uint8_t track, float value)
{
    if ((ui_param_seq_runtime_track_is_valid(track) == 0U)
            || (seq_edit_track_sequence_is_locked((seq_track_id_t)track) != 0U))
    {
        return 0U;
    }

    switch (param)
    {
        case PARAM_SEQ_DIV:
            seq_runtime_set_track_div(track,
                                       seq_division_track_div_from_ui((uint8_t)(ui_param_clamp(value, 0.0f, 3.0f) + 0.5f)));
            return 1U;

        case PARAM_SEQ_LENGTH:
            seq_model_set_track_length(track, (uint8_t)(ui_param_clamp(value, 1.0f, (float)SEQ_MAX_STEPS) + 0.5f));
            seq_runtime_on_track_length_changed(track);
            return 1U;

        case PARAM_SEQ_QUANT:
            seq_runtime_set_track_quant(track, (uint8_t)(ui_param_clamp(value, 0.0f, 100.0f) + 0.5f));
            return 1U;

        case PARAM_SEQ_SWING:
            seq_runtime_set_track_swing(track, (uint8_t)(ui_param_clamp(value, 0.0f, 100.0f) + 0.5f));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_param_relative_multi_track_is_record_context_blocked(void)
{
    if (seq_runtime_rec_is_armed() != 0U)
    {
        return 1U;
    }

    if (ui_hall_is_seq_context(ui_get_hall_mode()) != 0U)
    {
        seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
        seq_track_id_t held_track = 0U;
        if (seq_edit_collect_held_steps(&held_track, held_steps, (uint8_t)SEQ_STEPS_PER_PAGE, 1U) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static float ui_param_step_cfg_track(float current_value, int8_t direction, uint8_t active_track)
{
    if (direction == 0)
    {
        return current_value;
    }

    return (float)ui_track_catalog_cfg_family_step(
        (ui_track_family_t)(uint8_t)(current_value + 0.5f),
        direction,
        active_track,
        track_state_get_configs());
}

static float ui_param_step_cfg_track_type(float current_value,
                                          int8_t direction,
                                          uint8_t active_track)
{
    const ui_track_family_t active_family = ui_get_track_family(active_track);
    const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
    int16_t candidate = (int16_t)(current_value + 0.5f);

    if ((direction == 0) || (type_count == 0U))
    {
        return current_value;
    }

    candidate = (int16_t)(candidate + direction);
    if (candidate < 0)
    {
        candidate = 0;
    }
    if (candidate >= (int16_t)type_count)
    {
        candidate = (int16_t)type_count - 1;
    }

    return (float)candidate;
}



/**
 * @brief Point d'entrée ui_param_set_bank.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_set_bank.
 *
 * @param bank Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
