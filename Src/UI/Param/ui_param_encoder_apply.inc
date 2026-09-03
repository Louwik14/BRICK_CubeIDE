static uint8_t ui_param_set_active_track_value(uint8_t encoder, param_id_t param, float value, uint8_t active_track)
{
    return ui_param_set_track_value(encoder, param, value, active_track);
}

static uint8_t ui_param_desc_value_count_is_short(const param_desc_t *desc)
{
    if ((desc == 0) || (desc->step <= 0.0f) || (desc->max < desc->min))
    {
        return 0U;
    }

    const float span = desc->max - desc->min;
    const uint32_t intervals = (uint32_t)((span / desc->step) + 0.5f);
    return ((intervals + 1UL) < 20UL) ? 1U : 0U;
}

static uint8_t ui_param_encoder_uses_stepped_accum(const param_desc_t *desc)
{
    if (desc == 0)
    {
        return 0U;
    }

    if ((desc->type != PARAM_TYPE_ENUM)
            && (desc->type != PARAM_TYPE_BOOL)
            && (desc->type != PARAM_TYPE_INT)
            && (desc->display_type != PARAM_DISPLAY_ENUM)
            && (desc->display_type != PARAM_DISPLAY_BOOL)
            && (desc->display_type != PARAM_DISPLAY_INT))
    {
        return 0U;
    }

    return ui_param_desc_value_count_is_short(desc);
}

static uint32_t ui_param_make_stepped_encoder_key(const ui_param_encoder_context_t *ctx,
                                                  uint8_t encoder,
                                                  param_id_t param)
{
    uint32_t key = ((uint32_t)(ctx->active_track & 0x0FU) << 24)
                 | ((uint32_t)(encoder & 0x03U) << 22)
                 | ((uint32_t)param & 0x03FFUL);

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        key ^= ((uint32_t)ctx->bank.params[i] & 0x03FFUL) << (2U + (i * 5U));
    }
    return key;
}

static int16_t ui_param_filter_stepped_encoder_delta(const ui_param_encoder_context_t *ctx,
                                                     uint8_t encoder,
                                                     param_id_t param,
                                                     const param_desc_t *desc,
                                                     int16_t delta)
{
    if ((ctx == 0) || (encoder >= 4U) || (delta == 0) || (ui_param_encoder_uses_stepped_accum(desc) == 0U))
    {
        return delta;
    }

    const uint32_t key = ui_param_make_stepped_encoder_key(ctx, encoder, param);
    if (g_ui_param_stepped_encoder_key[encoder] != key)
    {
        g_ui_param_stepped_encoder_key[encoder] = key;
        g_ui_param_stepped_encoder_accum[encoder] = 0;
    }

    g_ui_param_stepped_encoder_accum[encoder] = (int16_t)(g_ui_param_stepped_encoder_accum[encoder] + delta);
    const int16_t stepped_delta = (int16_t)(g_ui_param_stepped_encoder_accum[encoder] / UI_PARAM_STEPPED_ENCODER_DIVIDER);
    g_ui_param_stepped_encoder_accum[encoder] =
        (int16_t)(g_ui_param_stepped_encoder_accum[encoder] - (stepped_delta * UI_PARAM_STEPPED_ENCODER_DIVIDER));
    return stepped_delta;
}

static uint8_t ui_param_apply_delta_value(param_id_t param,
                                          uint8_t track,
                                          float current_value,
                                          int16_t delta,
                                          float min_value,
                                          float max_value,
                                          uint8_t shift_down,
                                          float *out_value)
{
    if (out_value == 0) return 0U;
    if ((param == PARAM_STACK_OSC1_MODEL)
            || (param == PARAM_STACK_OSC2_MODEL)
            || (param == PARAM_STACK_OSC3_MODEL))
    {
        static const uint8_t simple_models[] = {0U, 1U, 3U};
        static const uint8_t parametric_models[] = {2U, 4U, 5U};
        const uint8_t *const models = (param == PARAM_STACK_OSC3_MODEL)
            ? parametric_models : simple_models;
        uint8_t pos = 0U;
        const uint8_t current = (uint8_t)(current_value + 0.5f);
        while ((pos < 3U) && (models[pos] != current)) ++pos;
        if (pos >= 3U)
        {
            pos = (delta > 0) ? 0U : 2U;
        }
        else
        {
            int32_t next = (int32_t)pos + (int32_t)delta;
            if (next < 0) next = 0;
            if (next > 2) next = 2;
            pos = (uint8_t)next;
        }
        *out_value = (float)models[pos];
        return 1U;
    }
    if((param==PARAM_AUDIO_FX_MODEL)||(param==PARAM_AUDIO_FX_B_MODEL))
    {
        static const uint8_t valid[]={0U,1U,2U,3U,5U,8U,10U,11U,12U,13U};
        uint8_t pos = 0U;
        const uint8_t current = (uint8_t)(current_value + 0.5f);
        while ((pos < 10U) && (valid[pos] != current)) ++pos;
        if (pos >= 10U)
        {
            pos = (delta > 0) ? 0U : 9U;
        }
        else
        {
            int32_t next = (int32_t)pos + delta;
            if (next < 0) next = 0;
            if (next > 9) next = 9;
            pos = (uint8_t)next;
        }
        float peer_value = 0.0f;
        const param_id_t peer = (param == PARAM_AUDIO_FX_MODEL)
            ? PARAM_AUDIO_FX_B_MODEL : PARAM_AUDIO_FX_MODEL;
        if (param_registry_get_track_value(peer, track, &peer_value) == 0U)
            return 0U;
        const uint8_t peer_model = (uint8_t)(peer_value + 0.5f);
        if ((valid[pos] != 0U) && (valid[pos] == peer_model))
        {
            const int8_t direction = (delta >= 0) ? 1 : -1;
            int32_t next = (int32_t)pos + direction;
            if (next < 0) next = 0;
            if (next > 9) next = 9;
            pos = (uint8_t)next;
        }
        *out_value = (float)valid[pos];
        return 1U;
    }
    const float next_value = param_value_policy_apply_delta(
        param, track, current_value, delta, shift_down, min_value, max_value);
    if (isnan(next_value))
        return 0U;
    *out_value = next_value;
    return 1U;
}

static uint8_t ui_param_apply_relative_delta_to_other_tracks(uint8_t encoder,
                                                             param_id_t param,
                                                             int16_t delta,
                                                             uint8_t fine,
                                                             uint8_t active_track)
{
    if ((delta == 0) || (ui_is_track_modifier_held() == 0U)
            || (ui_param_relative_multi_track_is_record_context_blocked() != 0U)
            || (ui_param_is_relative_multi_track_candidate(param, active_track) == 0U))
    {
        return 0U;
    }

    uint8_t applied = 0U;

    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        if (track == active_track)
        {
            continue;
        }

        if (ui_param_track_accepts_relative_param(track, param) == 0U)
        {
            continue;
        }

        float current_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 0.0f;
        if ((ui_param_get_track_edit_value(param, track, &current_value) == 0U)
                || (ui_param_resolve_edit_bounds(param, track, &min_value, &max_value) == 0U))
        {
            continue;
        }

        float next_value = 0.0f;
        if (ui_param_apply_delta_value(param, track, current_value, delta,
                                       min_value, max_value, fine,
                                       &next_value) == 0U)
            continue;
        if (ui_param_value_is_same(next_value, current_value) != 0U)
        {
            continue;
        }

        if (ui_param_set_track_value(encoder, param, next_value, track) != 0U)
        {
            applied = 1U;
        }
    }

    return applied;
}

static uint8_t ui_param_try_apply_seq_plock(uint8_t encoder,
                                            param_id_t param,
                                            const param_desc_t *desc,
                                            int16_t delta,
                                            float min_value,
                                            float max_value,
                                            uint8_t active_track)
{
    if ((ui_hall_is_seq_context(ui_get_hall_mode()) == 0U) || (desc == 0))
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           1U);
    if (held_count == 0U)
    {
        return 0U;
    }

    const seq_track_id_t param_track = active_track;
    if ((seq_edit_track_sequence_is_locked(param_track) != 0U)
            || (seq_edit_track_sequence_is_locked(held_track) != 0U))
    {
        return 1U;
    }

    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(param_track, param, &set_id, &param_slot) == 0U)
    {
        return 1U;
    }

    const float base_track_value = ui_param_get_active_track_value(param, active_track);
    seq_plock_entry_t prior_entries[SEQ_STEPS_PER_PAGE];
    seq_value16_t target_values[SEQ_STEPS_PER_PAGE];
    uint8_t had_prior_entry[SEQ_STEPS_PER_PAGE];
    for (uint8_t i = 0U; i < held_count; ++i)
    {
        const seq_step_id_t step = held_steps[i];

        float source_value = base_track_value;
        had_prior_entry[i] = ui_param_step_value_find(param_track, step, param,
                                                       set_id, param_slot, &prior_entries[i]);
        if (had_prior_entry[i] != 0U)
        {
            if (seq_param_iface_decode_param_value(
                    param, prior_entries[i].value16, &source_value) == 0U)
                return 0U;
        }

        float next_value = 0.0f;
        if (ui_param_apply_delta_value(param, param_track, source_value, delta,
                                       min_value, max_value,
                                       button_down(BTN_SHIFT) != 0U,
                                       &next_value) == 0U)
            return 1U;
        if (seq_param_iface_encode_param_value(
                param, next_value, &target_values[i]) == 0U)
            return 0U;
    }

    uint8_t applied_count = 0U;
    for (; applied_count < held_count; ++applied_count)
    {
        const seq_plock_op_status_t status = ui_param_step_value_upsert(param_track,
                                                                        held_steps[applied_count],
                                                                        param,
                                                                        set_id,
                                                                        param_slot,
                                                                        target_values[applied_count],
                                                                        0U);
        if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
        {
            continue;
        }

        while (applied_count > 0U)
        {
            applied_count--;
            if (had_prior_entry[applied_count] != 0U)
            {
                (void)ui_param_step_value_upsert(param_track,
                                                  held_steps[applied_count],
                                                  param,
                                                  set_id,
                                                  param_slot,
                                                  prior_entries[applied_count].value16,
                                                  prior_entries[applied_count].flags);
            }
            else
            {
                (void)ui_param_step_value_delete(param_track,
                                                  held_steps[applied_count],
                                                  param,
                                                  set_id,
                                                  param_slot);
            }
        }

        return 1U;
    }

    float flashed_value;
    if (seq_param_iface_decode_param_value(
            param, target_values[0], &flashed_value) == 0U)
        return 0U;
    ui_param_note_user_value_flash(encoder,
                                   param,
                                   param_track,
                                   flashed_value,
                                   UI_PARAM_VALUE_FLASH_PLOCK);

    return 1U;
}

static uint8_t ui_param_try_apply_live_rec_plock(uint8_t encoder,
                                                 param_id_t param,
                                                 const param_desc_t *desc,
                                                 int16_t delta,
                                                 float min_value,
                                                 float max_value,
                                                 uint8_t active_track)
{
    if ((desc == 0) || (ui_param_is_track_scoped(param) == 0U))
    {
        return 0U;
    }

    ui_param_live_rec_ctx_t live_rec_ctx;
    if (ui_param_live_rec_resolve_context(param, active_track, &live_rec_ctx) == 0U)
    {
        return 0U;
    }

    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    if (seq_edit_collect_held_steps(&held_track,
                                    held_steps,
                                    (uint8_t)SEQ_STEPS_PER_PAGE,
                                    1U) != 0U)
    {
        return 0U;
    }

    float source_value = ui_param_get_active_track_value(param, active_track);
    seq_plock_entry_t existing;
    const uint8_t before_present = ui_param_step_value_find(live_rec_ctx.track,
                                                            live_rec_ctx.step,
                                                            param,
                                                            live_rec_ctx.set_id,
                                                            live_rec_ctx.param_slot,
                                                            &existing);
    if (before_present != 0U)
    {
        if (seq_param_iface_decode_param_value(
                param, existing.value16, &source_value) == 0U)
            return 0U;
    }

    float next_value = 0.0f;
    if (ui_param_apply_delta_value(param, live_rec_ctx.track, source_value,
                                   delta, min_value, max_value,
                                   button_down(BTN_SHIFT) != 0U,
                                   &next_value) == 0U)
        return 0U;
    seq_value16_t encoded;
    if (seq_param_iface_encode_param_value(param, next_value, &encoded) == 0U)
        return 0U;

    const control_seq_intent_t intent = {
        .operation = CONTROL_SEQ_LIVE_REC_PLOCK_WRITE,
        .track = (uint8_t)live_rec_ctx.track,
        .set_id = live_rec_ctx.set_id,
        .param_slot = live_rec_ctx.param_slot,
        .value16 = encoded
    };
    if (control_domain_request_seq(&intent) == 0U)
    {
        return 0U;
    }

    ui_param_note_user_value_flash(encoder,
                                   param,
                                   active_track,
                                   next_value,
                                   UI_PARAM_VALUE_FLASH_LIVE_REC_PLOCK);
    return 1U;
}

uint8_t ui_param_try_get_seq_plock_feedback_with_frame(const ui_param_seq_plock_feedback_frame_t *frame_ctx,
                                                       param_id_t param,
                                                       float *out_value,
                                                       uint8_t *out_inverted)
{
    if (out_inverted != 0)
    {
        *out_inverted = 0U;
    }

    if (out_value == 0)
    {
        return 0U;
    }

    if ((frame_ctx == 0)
            || (frame_ctx->seq_context_active == 0U)
            || (frame_ctx->has_ref_step == 0U))
    {
        return 0U;
    }

    const uint8_t plock_track = ui_param_resolve_effective_edit_track(param, ui_get_active_lane());
    uint8_t set_id = 0U;
    seq_param_slot_t param_slot = 0U;
    if (ui_param_resolve_seq_slot(plock_track, param, &set_id, &param_slot) == 0U)
    {
        return 0U;
    }

    seq_plock_entry_t existing;
    if (ui_param_step_value_find(plock_track,
                                 frame_ctx->ref_step,
                                 param,
                                 set_id,
                                 param_slot,
                                 &existing) == 0U)
    {
        return 0U;
    }

    if (seq_param_iface_decode_param_value(
            param, existing.value16, out_value) == 0U)
        return 0U;
    if (out_inverted != 0)
    {
        *out_inverted = 1U;
    }

    return 1U;
}

