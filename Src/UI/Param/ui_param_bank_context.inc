void ui_param_set_bank(const ui_param_bank_t *bank)
{
    if (bank == 0)
    {
        g_ui_param.valid = 0U;
        g_ui_param_bank_track = 0xFFU;
        ui_param_clear_value_flash();
        ui_param_reset_stepped_encoder_accum();
        ui_param_publish_encoder_binding(ui_get_active_lane(),
                                         (uint8_t)(button_down(BTN_SHIFT) != 0U));
        return;
    }

    const uint8_t same_bank = ui_param_bank_is_same(bank);
    const uint8_t active_track = ui_get_active_lane();
    const uint8_t same_track = (g_ui_param_bank_track == active_track) ? 1U : 0U;
    g_ui_param.bank = *bank;
    g_ui_param_bank_track = active_track;
    g_ui_param.valid = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (g_ui_param.bank.params[i] < PARAM_COUNT)
        {
            g_ui_param.valid = 1U;
            break;
        }
    }

    if ((same_bank == 0U) || (same_track == 0U))
    {
        ui_param_clear_value_flash();
        ui_param_reset_stepped_encoder_accum();
    }

    ui_param_publish_encoder_binding(active_track,
                                     (uint8_t)(button_down(BTN_SHIFT) != 0U));
}

void ui_param_invalidate_bank(void)
{
    g_ui_param.valid = 0U;
    g_ui_param_bank_track = 0xFFU;
    ui_param_clear_value_flash();
    ui_param_reset_stepped_encoder_accum();
    ui_param_publish_encoder_binding(ui_get_active_lane(),
                                     (uint8_t)(button_down(BTN_SHIFT) != 0U));
}

void ui_param_sync_active_bank_values(void)
{
    const uint8_t active_track = ui_get_active_lane();

    if (g_ui_param.valid == 0U)
    {
        return;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        const param_id_t id = g_ui_param.bank.params[i];
        if (id >= PARAM_COUNT)
        {
            continue;
        }

        if (live_parameter_is_audio_owned(id) != 0U)
        {
            float value = 0.0f;
            if (ui_param_control_value_get(id, active_track, &value) != 0U)
            {
                param_store_set_active(id, value);
            }
            continue;
        }

        if (ui_param_is_track_scoped(id) != 0U)
        {
            /* Query seam: sync the UI mirror from the track-aware value surface. */
            float value = 0.0f;
            if (ui_param_get_track_edit_value(id, active_track, &value) != 0U)
            {
                param_store_set_active(id, value);
            }
            continue;
        }

        param_store_set_active(id, param_get(id));
    }
}

void ui_param_sync_active_track_mirror_from_runtime(void)
{
    const uint8_t active_track = ui_get_active_lane();

    const float seq_length = (float)seq_model_get_track_length(active_track);
    uint8_t track_div = 1U;
    uint8_t track_quant = 0U;
    uint8_t track_swing = 0U;

    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            continue;
        }

        if ((id >= PARAM_FILTER_MORPH) && (id <= PARAM_FILTER_DECIMATOR_RATE2))
        {
            continue;
        }

        if (live_parameter_is_audio_owned(id) != 0U)
        {
            float value = 0.0f;
            if (ui_param_control_value_get(id, active_track, &value) != 0U)
            {
                param_store_set_active(id, value);
            }
            continue;
        }

        /* Query seam: read track-aware value without mutating the runtime. */
        float value = 0.0f;
        if (param_registry_get_track_value(id, active_track, &value) != 0U)
        {
            param_store_set_active(id, value);
        }
    }

    param_store_set_active(PARAM_SEQ_LENGTH, seq_length);
    if (seq_runtime_get_track_div(active_track, &track_div) != 0U)
    {
        param_store_set_active(PARAM_SEQ_DIV, (float)seq_division_track_div_to_ui(track_div));
    }
    if (seq_runtime_get_track_quant(active_track, &track_quant) != 0U)
    {
        param_store_set_active(PARAM_SEQ_QUANT, (float)track_quant);
    }
    if (seq_runtime_get_track_swing(active_track, &track_swing) != 0U)
    {
        param_store_set_active(PARAM_SEQ_SWING, (float)track_swing);
    }
    param_registry_sync_filter_ui_for_active_track();
}

void ui_param_capture_encoder_context(ui_param_encoder_context_t *out_ctx)
{
    if (out_ctx == 0)
    {
        return;
    }

    ui_param_capture_encoder_context_for_state(out_ctx,
                                               ui_get_active_lane(),
                                               (uint8_t)(button_down(BTN_SHIFT) != 0U));
}

void ui_param_capture_encoder_context_for_state(ui_param_encoder_context_t *out_ctx,
                                                uint8_t active_track,
                                                uint8_t shift_down)
{
    if (out_ctx == 0)
    {
        return;
    }

    out_ctx->bank = g_ui_param.bank;
    out_ctx->valid = g_ui_param.valid;
    out_ctx->active_track = active_track;
    out_ctx->shift_down = (shift_down != 0U) ? 1U : 0U;
}

void ui_param_begin_encoder_edit_group(const ui_param_encoder_context_t *ctx)
{
    (void)ctx;
}

void ui_param_end_encoder_edit_group(void)
{
}

uint8_t ui_param_get_active_bank_param(uint8_t encoder, param_id_t *out_param)
{
    if ((out_param == 0) || (g_ui_param.valid == 0U) || (encoder >= 4U))
    {
        return 0U;
    }

    *out_param = g_ui_param.bank.params[encoder];
    return (*out_param < PARAM_COUNT) ? 1U : 0U;
}

static uint8_t ui_param_seq_resolve_ref_step(seq_track_id_t *out_track,
                                             seq_step_id_t *out_ref_step,
                                             uint8_t promote_pending)
{
    if ((out_track == 0) || (out_ref_step == 0) || (ui_hall_is_seq_context(ui_get_hall_mode()) == 0U))
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           promote_pending);
    if (held_count == 0U)
    {
        return 0U;
    }

    seq_step_id_t ref_step = held_steps[0];
    for (uint8_t i = 1U; i < held_count; ++i)
    {
        if (held_steps[i] < ref_step)
        {
            ref_step = held_steps[i];
        }
    }

    *out_track = held_track;
    *out_ref_step = ref_step;
    return 1U;
}

void ui_param_seq_plock_feedback_frame_begin(ui_param_seq_plock_feedback_frame_t *frame_ctx)
{
    if (frame_ctx == 0)
    {
        return;
    }

    frame_ctx->seq_context_active = 0U;
    frame_ctx->has_ref_step = 0U;
    frame_ctx->ref_track = 0U;
    frame_ctx->ref_step = 0U;

    if (ui_hall_is_seq_context(ui_get_hall_mode()) == 0U)
    {
        return;
    }

    frame_ctx->seq_context_active = 1U;

    seq_track_id_t ref_track = 0U;
    seq_step_id_t ref_step = 0U;
    if (ui_param_seq_resolve_ref_step(&ref_track, &ref_step, 0U) == 0U)
    {
        return;
    }

    frame_ctx->has_ref_step = 1U;
    frame_ctx->ref_track = ref_track;
    frame_ctx->ref_step = ref_step;
}

