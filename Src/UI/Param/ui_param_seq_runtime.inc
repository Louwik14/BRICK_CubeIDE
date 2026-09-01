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
