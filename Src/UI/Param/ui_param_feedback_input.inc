uint8_t ui_param_try_get_seq_plock_feedback(param_id_t param, float *out_value, uint8_t *out_inverted)
{
    ui_param_seq_plock_feedback_frame_t frame_ctx;
    ui_param_seq_plock_feedback_frame_begin(&frame_ctx);
    return ui_param_try_get_seq_plock_feedback_with_frame(&frame_ctx, param, out_value, out_inverted);
}

/**
 * @brief Point d'entrée ui_param_handle_encoder.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_handle_encoder.
 *
 * @param encoder Paramètre d'entrée de l'API.
 * @param delta Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_param_handle_encoder(uint8_t encoder, int16_t delta)
{
    ui_param_encoder_context_t ctx;
    ui_param_capture_encoder_context(&ctx);
    (void)ui_param_handle_encoder_with_context(&ctx, encoder, delta);
}

uint8_t ui_param_handle_encoder_with_context(const ui_param_encoder_context_t *ctx,
                                             uint8_t encoder,
                                             int16_t delta)
{
    if ((ctx == 0) || (ctx->valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return 0U;
    }

    const param_id_t param = ctx->bank.params[encoder];
    if (ui_param_is_local_control(param) != 0U)
        return ui_param_local_control_apply(param, ctx->active_track, delta);
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }
    const uint8_t edit_track = ui_param_resolve_effective_edit_track(param, ctx->active_track);

    const param_desc_t *desc = &param_registry[param];
    float min_value = 0.0f;
    float max_value = 0.0f;
    (void)ui_param_resolve_edit_bounds(param, edit_track, &min_value, &max_value);

    if (ui_param_try_apply_seq_plock(encoder, param, desc, delta, min_value, max_value,
                                     edit_track, ctx->shift_down) != 0U)
    {
        return 1U;
    }

    float value = ui_param_get_active_track_value(param, ctx->active_track);
    float edit_source = value;
    const uint8_t scene_capture = ui_macro_interaction_get_edit_source(
        param, edit_track, value, &edit_source);
    if ((scene_capture != 0U) && (ctx->shift_down != 0U))
    {
        return ui_macro_interaction_commit_edit(encoder, param, edit_track,
                                                edit_source, 1U);
    }

    if ((scene_capture == 0U)
            && (ui_param_try_apply_live_rec_plock(encoder, param, desc, delta,
                                                  min_value, max_value,
                                                  edit_track) != 0U))
    {
        return 1U;
    }

    delta = ui_param_filter_stepped_encoder_delta(ctx, encoder, param, desc, delta);
    if (delta == 0)
    {
        return scene_capture;
    }

    const float source_current_value = edit_source;
    {
        if (ui_param_apply_delta_value(param, edit_track,
                                       source_current_value, delta,
                                       min_value, max_value,
                                       ctx->shift_down, &value) == 0U)
            return 0U;

        if (ui_param_value_is_same(value, source_current_value) != 0U)
        {
            if (scene_capture != 0U)
            {
                return 1U;
            }
            const uint8_t applied = ui_param_apply_relative_delta_to_other_tracks(encoder, param, delta, ctx->shift_down, edit_track);
            return applied;
        }
    }

    if (scene_capture != 0U)
    {
        return ui_macro_interaction_commit_edit(encoder, param, edit_track,
                                                value, 0U);
    }

    uint8_t source_applied = ui_param_set_active_track_value(encoder, param, value, edit_track);
    if (source_applied != 0U)
    {
        ui_param_note_user_value_flash(encoder,
                                       param,
                                       edit_track,
                                       value,
                                       UI_PARAM_VALUE_FLASH_DIRECT);
    }
    (void)ui_param_apply_relative_delta_to_other_tracks(encoder, param, delta, ctx->shift_down, edit_track);
    return 1U;
}

uint8_t ui_param_resolve_encoder_detent_from_binding(param_id_t param,
                                                     uint8_t scope,
                                                     uint8_t track,
                                                     uint8_t slot,
                                                     uint8_t shift_down,
                                                     int8_t direction,
                                                     float current_value,
                                                     ui_param_encoder_target_t *out_target)
{
    if ((out_target == 0) || (param >= PARAM_COUNT)
            || ((direction != -1) && (direction != 1)))
    {
        return 0U;
    }

    if ((scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) && (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    ui_param_encoder_context_t binding_context = {
        .bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        .valid = 1U,
        .active_track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) ? track : 0U,
        .shift_down = (shift_down != 0U) ? 1U : 0U
    };
    float min_value = 0.0f;
    float max_value = 0.0f;
    if (ui_param_resolve_edit_bounds(param,
                                     binding_context.active_track,
                                     &min_value,
                                     &max_value) == 0U)
    {
        return 0U;
    }

    out_target->parameter_id = param;
    out_target->scope = scope;
    out_target->track = (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK) ? track : 0U;
    out_target->slot = slot;
    if (ui_param_apply_delta_value(param, binding_context.active_track,
                                   current_value, direction,
                                   min_value, max_value,
                                   binding_context.shift_down,
                                   &out_target->value) == 0U)
        return 0U;
    return 1U;
}
