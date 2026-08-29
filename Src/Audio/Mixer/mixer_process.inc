ITCM_TEXT void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    AUDIO_HOT ALIGN32 static float mono_pan_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float mono_pan_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_fx_l[2][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_fx_r[2][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float audio_rec_bus_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float audio_rec_bus_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static int32_t looper_record_i32[AUDIO_BLOCK_SIZE * AUDIO_RECORDER_CHANNELS];
    AUDIO_HOT ALIGN32 static int32_t audio_rec_bus_i32[AUDIO_BLOCK_SIZE * AUDIO_RECORDER_CHANNELS];
    static uint8_t looper_output_active[MIXER_MAX_TRACKS];

    audio_rec_bus_control_snapshot_t audio_rec_projection = {0};
    const uint8_t audio_rec_projection_valid =
        audio_rec_bus_projection_audio_read(&audio_rec_projection);

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    const brick_entity_id_t waveform_entity = audio_waveform_capture_get_entity();
    audio_waveform_capture_begin_block(waveform_entity);

    const uint8_t group_active = g_mixer_static_group_active;


    const uint8_t reverb_active = fx_reverb_global_is_active();
    const uint8_t delay_active = (g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            ? fx_delay_dual_global_is_active()
            : fx_delay_stereo_global_is_active();
    const uint8_t delay_reverb_send_active = (delay_active != 0U)
        ? ((g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            ? fx_delay_dual_global_reverb_send_is_active()
            : fx_delay_stereo_global_reverb_send_is_active())
        : 0U;
    const uint8_t modfx_active = fx_modfx_global_is_active();
    /* An enabled send FX remains in the process mask with zero input so its
     * history/tail keeps advancing exactly as before. Input fanout is a
     * separate decision based only on non-zero current/target lane gains. */
    const uint8_t tail_process_mask = mixer_send_destination_process_mask(
        reverb_active, delay_active, modfx_active);
    const uint32_t valid_lane_mask = (MIXER_MAX_TRACKS >= 32U)
        ? UINT32_MAX : ((uint32_t)(1UL << MIXER_MAX_TRACKS) - 1U);
    uint32_t lane_mask = (g_external_lane_mask | audio_tracks_enabled_mask())
        & valid_lane_mask;
    uint8_t lane_send_masks[MIXER_MAX_TRACKS] = {0U};
    uint8_t group_send_mask = 0U;
    const uint8_t input_send_mask = mixer_collect_input_send_masks(
        lane_mask, group_active, tail_process_mask,
        lane_send_masks, &group_send_mask);
    const uint8_t send_bus_active = (tail_process_mask != 0U) ? 1U : 0U;
    uint8_t send_written_mask = 0U;
    uint8_t group_fx_written_mask = 0U;

    memset(bus_main_l, 0, sizeof(bus_main_l));
    memset(bus_main_r, 0, sizeof(bus_main_r));
    if (group_active != 0U)
    {
        memset(bus_group_l, 0, sizeof(bus_group_l));
        memset(bus_group_r, 0, sizeof(bus_group_r));
    }
    const uint32_t ntracks = (track_count < MIXER_MAX_TRACKS) ? track_count : MIXER_MAX_TRACKS;
    const float looper_xfade_target = audio_xfade_get();
    const uint8_t looper_xfade_process_active =
        ((looper_xfade_target > MIXER_LOOPER_XFADE_EPS)
                || (g_looper_xfade_prev > MIXER_LOOPER_XFADE_EPS)
                || (g_looper_xfade_smoothed > MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
    float looper_xfade_start = g_looper_xfade_prev;
    float looper_xfade_end = g_looper_xfade_prev;
    uint8_t looper_xfade_apply_active = 0U;
    if(looper_xfade_process_active != 0U)
    {
        looper_xfade_end = mixer_get_looper_xfade();
        looper_xfade_start = g_looper_xfade_prev;
        g_looper_xfade_prev = looper_xfade_end;

        if((mixer_looper_xfade_value_is_zero(looper_xfade_start) == 0U)
                || (mixer_looper_xfade_value_is_zero(looper_xfade_end) == 0U))
        {
            looper_xfade_apply_active = 1U;
        }
        else
        {
            g_looper_xfade_prev = 0.0f;
            g_looper_xfade_smoothed = 0.0f;
            looper_xfade_start = 0.0f;
            looper_xfade_end = 0.0f;
        }
    }
    uint8_t looper_playback_active = 0U;
    uint8_t looper_playback_mix_active = 0U;
    uint8_t looper_playback_routes_main = 0U;
    uint16_t looper_mask = brick6_looper_runtime_playing_mask();
    if (looper_mask != 0U)
    {
        memset(looper_output_active, 0, sizeof(looper_output_active));
    }
    while (looper_mask != 0U)
    {
        const uint8_t logical_track = (uint8_t)__builtin_ctz((unsigned)looper_mask);
        looper_mask &= (uint16_t)(looper_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(
                logical_track, &ctx_value) != 0U) ? &ctx_value : NULL;
        if((ctx != 0)
                && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS)
                && ((g_mixer_static_lane_flags[ctx->program_route.mix_track_id]
                    & MIXER_STATIC_LOOPER) != 0U)
                && (g_tracks[ctx->program_route.mix_track_id].mute == 0U)
                && (brick6_looper_runtime_is_playing(logical_track) != 0U))
        {
            looper_output_active[logical_track] = 1U;
            looper_playback_active = 1U;
            if((g_mixer_static_lane_flags[ctx->program_route.mix_track_id]
                    & MIXER_STATIC_ROUTE_MAIN) != 0U)
            {
                looper_playback_routes_main = 1U;
            }
        }
    }
    looper_playback_mix_active =
        ((looper_playback_active != 0U) && (looper_xfade_apply_active != 0U)) ? 1U : 0U;
    uint8_t looper_record_track = 0U;
    const uint8_t looper_record_active = mixer_looper_record_capture_is_active(&looper_record_track);
    const uint8_t audio_rec_capture_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && ((audio_rec_projection.source_flags & AUDIO_REC_BUS_CAPTURE_ENABLED) != 0U));
    const uint8_t audio_rec_track_routes_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && (audio_rec_projection.source_entity_mask != 0U));
    const uint8_t audio_rec_bus_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && ((audio_rec_capture_active != 0U)
            || (audio_rec_projection.source_entity_mask != 0U)
            || ((audio_rec_projection.source_flags
                & (AUDIO_REC_BUS_SOURCE_LINE_DIRECT
                    | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL)) != 0U)));
    if((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
    {
        memset(looper_bus_main_l, 0, sizeof(looper_bus_main_l));
        memset(looper_bus_main_r, 0, sizeof(looper_bus_main_r));
    }
    if(looper_record_active != 0U)
    {
        memset(looper_record_l, 0, sizeof(looper_record_l));
        memset(looper_record_r, 0, sizeof(looper_record_r));
    }
    if(audio_rec_bus_active != 0U)
    {
        memset(audio_rec_bus_l, 0, sizeof(audio_rec_bus_l));
        memset(audio_rec_bus_r, 0, sizeof(audio_rec_bus_r));
    }

    while (lane_mask != 0U)
    {
        const uint32_t t = (uint32_t)__builtin_ctz(lane_mask);
        lane_mask &= lane_mask - 1U;
        mixer_audio_track_runtime_t *mt = &g_tracks[t];
        const uint8_t static_flags = g_mixer_static_lane_flags[t];
        const brick_entity_id_t source_entity =
            audio_note_engine_adapter_entity_for_mix_lane((uint8_t)t);
        const uint8_t group_child = (uint8_t)(
            (static_flags & MIXER_STATIC_GROUP_CHILD) != 0U);
        const brick_entity_id_t audio_fx_entity = source_entity;
        const uint8_t audio_fx_active = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_ACTIVE) != 0U);
        const uint8_t filter_deferred = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_PRE_FILTER) != 0U);
        const uint8_t route_main = (uint8_t)(
            (static_flags & MIXER_STATIC_ROUTE_MAIN) != 0U);
        const uint8_t requires_stereo = (uint8_t)(
            (static_flags & MIXER_STATIC_REQUIRES_STEREO) != 0U);
        const uint8_t insert_stage = (uint8_t)(
            (static_flags & MIXER_STATIC_INSERT_STAGE) != 0U);
        const uint8_t audio_fx_comp = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_COMP) != 0U);
        const uint8_t audio_fx_post = (uint8_t)(
            (audio_fx_active != 0U)
            && (audio_fx_comp == 0U));
        const audio_fx_sample_plan_handle_t audio_fx_sample_plan =
            (audio_fx_post != 0U)
                ? audio_fx_runtime_get_sample_plan(audio_fx_entity) : NULL;
        float *const dry_bus_l = (group_child != 0U) ? bus_group_l : bus_main_l;
        float *const dry_bus_r = (group_child != 0U) ? bus_group_r : bus_main_r;
        const uint8_t hw_enabled = (t < ntracks) ? tracks[t].enabled : 0U;
        const mixer_lane_plan_t lane_plan = mixer_build_lane_plan(t,
                                                                  mt,
                                                                  &g_track_filters[t],
                                                                  hw_enabled,
                                                                  g_external_track_enabled[t],
                                                                  g_external_track_format[t],
                                                                  g_external_track_frames_valid[t],
                                                                  frames);
        if (lane_plan.active == 0U)
            continue;
        float *L = NULL;
        float *R = NULL;
        float *mono = NULL;
        const uint8_t is_mono_native_lane = (lane_plan.exec_kind == MIXER_LANE_EXEC_MONO_NATIVE) ? 1U : 0U;
        const uint8_t waveform_capture_lane =
            ((waveform_entity != BRICK_ENTITY_INVALID_ID)
             && (source_entity == waveform_entity)) ? 1U : 0U;
        const uint8_t multi_prefiltered =
            ((lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)
             || (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_STEREO)) ? 1U : 0U;

        if (is_mono_native_lane != 0U)
        {
            mixer_lane_buffers_t buffers = {0};
            if (waveform_capture_lane != 0U)
            {
                audio_waveform_capture_tap_reference_mono_block(
                    g_external_track_mono[t], frames);
            }
            if (multi_prefiltered != 0U)
            {
                buffers.mono = g_external_track_mono[t];
            }
            else
            {
                if (filter_deferred == 0U)
                    buffers = mixer_lane_run_mono_native_path(t,
                                                               mt,
                                                               &g_track_filters[t],
                                                               frames);
                else
                    buffers.mono = g_external_track_mono[t];
            }
            mono = buffers.mono;
        }
        else
        {
            const mixer_lane_buffers_t buffers = mixer_lane_prepare_stereo_buffers(t,
                                                                                   &lane_plan,
                                                                                   tracks);
            L = buffers.left;
            R = buffers.right;
            mixer_lane_accumulate_external_source(t, &lane_plan, L, R);
            if (waveform_capture_lane != 0U)
            {
                audio_waveform_capture_tap_reference_stereo_block(L, R, frames);
            }
            if ((filter_deferred == 0U)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO))
                mixer_lane_run_stereo_path(t, mt, &g_track_filters[t], L, R, frames);
        }
        const uint8_t waveform_capture_samples = (uint8_t)(
            (waveform_capture_lane != 0U)
            && (audio_waveform_capture_needs_final_samples() != 0U));


        /*
         * Common mono-native fan-out. Keep the historical L/R path when
         * an insert, diagnostic, or auxiliary capture/looper route needs the
         * materialized stereo buffers.  The direct path preserves the same
         * post-pan L/R values and the existing stereo send contracts, but
         * accumulates every destination while the source sample is live.
         */
        uint8_t direct_mono_fanout = 0U;
        const uint8_t looper_record_route_active =
            (looper_record_active != 0U)
                ? mixer_lane_routes_to_looper(looper_record_track, (uint8_t)t)
                : 0U;
        if ((is_mono_native_lane != 0U)
                && (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                && (audio_rec_track_routes_active == 0U)
                && (looper_record_route_active == 0U)
                && (looper_playback_mix_active == 0U))
        {
            direct_mono_fanout = (requires_stereo == 0U) ? 1U : 0U;
        }

        if (direct_mono_fanout != 0U)
        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            const uint8_t track_vca_enabled =
                (g_track_filters[t].vca_enabled != 0U) ? 1U : 0U;
            float vca_segment_gain[AUDIO_BLOCK_SIZE];
            uint32_t vca_frames = 0U;
            if (track_vca_enabled != 0U)
            {
                vca_frames = env_adsr_process_vca_block(&g_track_filters[t].vca_env,
                                                        vca_segment_gain,
                                                        frames);
                for (uint32_t i = vca_frames; i < frames; ++i)
                {
                    vca_segment_gain[i] = 0.0f;
                }
            }
            const uint8_t first_writer_mask = (uint8_t)(
                lane_send_masks[t] & (uint8_t)~send_written_mask);
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                mt, lane_send_masks[t], first_writer_mask, inv_frames);
            mixer_group_fx_plan_t group_fx_plan = mixer_prepare_group_fx_plan(
                mt, group_child, &group_fx_written_mask, inv_frames);

            for (uint32_t i = 0U; i < frames; ++i)
            {
                /* Keep the historical ramp and pan equations/order. */
                float pan_l = coefficient_plan.pan_l;
                float pan_r = coefficient_plan.pan_r;
                float mono_gain = coefficient_plan.mono_gain;
                if (coefficient_plan.stable == 0U)
                {
                    const float pan_for_mix = -pan_cur;
                    pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    mono_gain = gain_cur * mute_gain_cur;
                }
                float vca_gain = 1.0f;
                if (track_vca_enabled != 0U)
                {
                    vca_gain = vca_segment_gain[i];
                    g_track_filters[t].vca_env_value = vca_gain;
                }

                float sample_pre_fader = mono[i] * vca_gain;
                if (audio_fx_comp != 0U)
                {
                    sample_pre_fader = fx_chain_process_audio_fx_comp_mono_sample(
                        audio_fx_entity, sample_pre_fader);
                }
                const float sample_processed = sample_pre_fader * mono_gain;
                float left = sample_processed * pan_l;
                float right = sample_processed * pan_r;
                if (audio_fx_post != 0U)
                {
                    audio_fx_runtime_process_stereo_sample_prepared(
                        audio_fx_sample_plan, &left, &right);
                }
                if (waveform_capture_samples != 0U)
                {
                    audio_waveform_capture_tap_stereo_sample(left, right);
                }
                const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                if (group_fx_plan.mask != 0U)
                    mixer_accumulate_group_fx_sample(&group_fx_plan,
                        bus_group_fx_l, bus_group_fx_r, i,
                        left_trimmed, right_trimmed);
                if (send_plan.assign_mask == 0U)
                {
                    switch (send_plan.count)
                    {
                    case 3U:
                    {
                        const uint8_t send = send_plan.index[2];
                        send_l[send][i] += left_trimmed * send_plan.current[2];
                        send_r[send][i] += right_trimmed * send_plan.current[2];
                        send_plan.current[2] += send_plan.step[2];
                    }
                    /* fall through */
                    case 2U:
                    {
                        const uint8_t send = send_plan.index[1];
                        send_l[send][i] += left_trimmed * send_plan.current[1];
                        send_r[send][i] += right_trimmed * send_plan.current[1];
                        send_plan.current[1] += send_plan.step[1];
                    }
                    /* fall through */
                    case 1U:
                    {
                        const uint8_t send = send_plan.index[0];
                        send_l[send][i] += left_trimmed * send_plan.current[0];
                        send_r[send][i] += right_trimmed * send_plan.current[0];
                        send_plan.current[0] += send_plan.step[0];
                        break;
                    }
                    default:
                        break;
                    }
                }
                else
                {
                    for (uint32_t active = 0U; active < send_plan.count; ++active)
                    {
                        const uint8_t send = send_plan.index[active];
                        const float send_gain = send_plan.current[active];
                        if ((send_plan.assign_mask & MIXER_SEND_MASK_BIT(active)) != 0U)
                        {
                            send_l[send][i] = left_trimmed * send_gain;
                            send_r[send][i] = right_trimmed * send_gain;
                        }
                        else
                        {
                            send_l[send][i] += left_trimmed * send_gain;
                            send_r[send][i] += right_trimmed * send_gain;
                        }
                        send_plan.current[active] += send_plan.step[active];
                    }
                }

                if (route_main != 0U)
                {
                    dry_bus_l[i] += left_trimmed;
                    dry_bus_r[i] += right_trimmed;
                }

                if (coefficient_plan.stable == 0U)
                {
                    mixer_advance_track_ramps(&gain_cur, gain_step,
                                              &pan_cur, pan_step,
                                              &mute_gain_cur, mute_target, mute_step);
                }
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
            if (send_bus_active != 0U)
                mixer_finish_send_levels(mt);
            mixer_finish_group_fx_levels(mt, &group_fx_plan);
            send_written_mask |= lane_send_masks[t];
            continue;
        }
        /*
         * POLY_STEREO final fan-out.  The external poly buffers are already
         * accumulated L/R, so keep them read-only and feed every active final
         * destination from one pass.  Auxiliary capture/looper routes and
         * diagnostics keep the historical materialized-buffer path.
         */
        uint8_t poly_stereo_fanout = 0U;
        if ((lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                && (audio_rec_track_routes_active == 0U)
                && (looper_record_route_active == 0U)
                && (looper_playback_mix_active == 0U)
                && (route_main != 0U)
                && (modfx_active == 0U)
                && ((input_send_mask & MIXER_SEND_MASK_BIT(MIXER_MODFX_SEND_INDEX)) == 0U)
                )
        {
            poly_stereo_fanout = (requires_stereo == 0U) ? 1U : 0U;
        }

        if (poly_stereo_fanout != 0U)
        {
            /* No pooled insert is active in this fast path, but the
             * mono-compatible entity Audio FX must run after the per-frame
             * gain/pan coefficients, just like the regular insert path. */
            const brick_entity_id_t audio_fx_entity =
                (source_entity < BRICK_ENTITY_CAPACITY)
                    ? (brick_entity_id_t)source_entity
                    : BRICK_ENTITY_INVALID_ID;
            enum
            {
                POLY_FANOUT_MAIN = 1U,
                POLY_FANOUT_REVERB = 2U,
                POLY_FANOUT_DELAY = 4U
            };
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            float send_cur[MIXER_NUM_SENDS] = {0.0f};
            float send_step[MIXER_NUM_SENDS] = {0.0f};
            uint8_t fanout_mode = POLY_FANOUT_MAIN;
            uint8_t fanout_assign_mask = 0U;
            mixer_group_fx_plan_t group_fx_plan = mixer_prepare_group_fx_plan(
                mt, group_child, &group_fx_written_mask, inv_frames);

            if (send_bus_active != 0U)
            {
                const mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                    mt, lane_send_masks[t],
                    (uint8_t)(lane_send_masks[t] & (uint8_t)~send_written_mask),
                    inv_frames);
                fanout_assign_mask = send_plan.assign_mask;
                for (uint32_t active = 0U; active < send_plan.count; ++active)
                {
                    const uint8_t send = send_plan.index[active];
                    send_cur[send] = send_plan.current[active];
                    send_step[send] = send_plan.step[active];
                    fanout_mode |= (send == MIXER_REVERB_SEND_INDEX)
                        ? POLY_FANOUT_REVERB : POLY_FANOUT_DELAY;
                }
            }

            switch (fanout_mode)
            {
                case POLY_FANOUT_MAIN:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_REVERB:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_REVERB_SEND_INDEX] += send_step[MIXER_REVERB_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_DELAY:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_DELAY_SEND_INDEX] += send_step[MIXER_DELAY_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_REVERB | POLY_FANOUT_DELAY:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(1U)) != 0U)
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_REVERB_SEND_INDEX] += send_step[MIXER_REVERB_SEND_INDEX];
                        send_cur[MIXER_DELAY_SEND_INDEX] += send_step[MIXER_DELAY_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                default:
                    break;
            }


            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
            if (send_bus_active != 0U)
                mixer_finish_send_levels(mt);
            mixer_finish_group_fx_levels(mt, &group_fx_plan);
            send_written_mask |= lane_send_masks[t];
            continue;
        }

        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            const uint8_t track_vca_enabled =
                ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                    && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                    && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                    && (g_track_filters[t].vca_enabled != 0U)) ? 1U : 0U;
            float vca_segment_gain[AUDIO_BLOCK_SIZE];
            uint32_t vca_frames = 0U;
            if (track_vca_enabled != 0U)
            {
                vca_frames = env_adsr_process_vca_block(&g_track_filters[t].vca_env,
                                                        vca_segment_gain,
                                                        frames);
                for (uint32_t i = vca_frames; i < frames; ++i)
                {
                    vca_segment_gain[i] = 0.0f;
                }
            }

            for (uint32_t i = 0U; i < frames; ++i)
            {
                float vca_gain = 1.0f;
                if (track_vca_enabled != 0U)
                {
                    vca_gain = vca_segment_gain[i];
                    g_track_filters[t].vca_env_value = vca_gain;
                }
                if (is_mono_native_lane != 0U)
                {
                    const float vca_sample = mono[i] * vca_gain;
                    mono[i] = vca_sample;
                    mono_pan_l[i] = vca_sample;
                    mono_pan_r[i] = vca_sample;
                }
                else
                {
                    L[i] *= vca_gain;
                    R[i] *= vca_gain;
                }
            }

            const brick_entity_id_t audio_fx_entity =
                (source_entity < BRICK_ENTITY_CAPACITY)
                    ? (brick_entity_id_t)source_entity
                    : BRICK_ENTITY_INVALID_ID;
            if (is_mono_native_lane != 0U)
            {
                L = mono_pan_l;
                R = mono_pan_r;
            }
            if (insert_stage != 0U)
            {
                fx_chain_process_track_inserts_pre_fader(
                    audio_fx_entity,
                    t,
                    mt->insert_slot,
                    MIXER_INSERTS_PER_TRACK,
                    audio_fx_comp,
                    L,
                    R,
                    frames);
            }

            gain_cur = mt->gain_current;
            pan_cur = mt->pan_current;
            mute_gain_cur = mt->mute_gain_current;
            for (uint32_t i = 0U; i < frames; ++i)
            {
                /* Standard user convention: pan<0 => left, pan>0 => right.
                 * Runtime output stage wiring is mirrored, so mixer pan is compensated here. */
                float pan_l = coefficient_plan.pan_l;
                float pan_r = coefficient_plan.pan_r;
                float gain_l = coefficient_plan.gain_l;
                float gain_r = coefficient_plan.gain_r;
                float mono_gain = coefficient_plan.mono_gain;
                if (coefficient_plan.stable == 0U)
                {
                    const float pan_for_mix = -pan_cur;
                    pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    gain_l = gain_cur * pan_l * mute_gain_cur;
                    gain_r = gain_cur * pan_r * mute_gain_cur;
                    mono_gain = gain_cur * mute_gain_cur;
                }
                if (is_mono_native_lane != 0U)
                {
                    mono_pan_l[i] = L[i] * (mono_gain * pan_l);
                    mono_pan_r[i] = R[i] * (mono_gain * pan_r);
                }
                else
                {
                    L[i] *= gain_l;
                    R[i] *= gain_r;
                }
                if (coefficient_plan.stable == 0U)
                {
                    mixer_advance_track_ramps(&gain_cur, gain_step,
                                              &pan_cur, pan_step,
                                              &mute_gain_cur, mute_target, mute_step);
                }
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
        }

        if (is_mono_native_lane != 0U)
        {
            L = mono_pan_l;
            R = mono_pan_r;
        }

        /* engine -> filter -> VCA -> pooled inserts/COMP -> track fader/mute
         * -> pan -> remaining Audio FX -> sends/bus. */
        if ((audio_fx_post != 0U) || (filter_deferred != 0U))
        {
            audio_fx_runtime_process_before_filter(audio_fx_entity,L,R,frames);
            if (filter_deferred != 0U)
                mixer_lane_run_stereo_path(t,mt,&g_track_filters[t],L,R,frames);
            audio_fx_runtime_process_after_filter(audio_fx_entity,L,R,frames);
        }
        if (waveform_capture_samples != 0U)
        {
            audio_waveform_capture_tap_stereo_block(L, R, frames);
        }

        if (group_child != 0U)
        {
            mixer_accumulate_group_fx(mt, L, R,
                                      bus_group_fx_l, bus_group_fx_r,
                                      frames, &group_fx_written_mask);
        }

        {
            const uint8_t source_track = (source_entity < BRICK_ENTITY_CAPACITY)
                ? source_entity : (uint8_t)t;
            if((source_track < MIXER_MAX_TRACKS)
                    && ((static_flags & MIXER_STATIC_LOOPER) != 0U))
            {
                if((audio_rec_bus_active != 0U)
                        && (source_track < BRICK_ENTITY_CAPACITY)
                        && ((audio_rec_projection.source_entity_mask
                            & (uint16_t)(1U << source_track)) != 0U))
                {
                    for(uint32_t i = 0U; i < frames; ++i)
                    {
                        audio_rec_bus_l[i] += L[i];
                        audio_rec_bus_r[i] += R[i];
                    }
                }

                if((looper_playback_mix_active != 0U) && (looper_output_active[source_track] != 0U))
                {
                    if(route_main != 0U)
                    {
                        for(uint32_t i = 0U; i < frames; ++i)
                        {
                            looper_bus_main_l[i] += L[i] * MIXER_TRACK_NOMINAL_TRIM;
                            looper_bus_main_r[i] += R[i] * MIXER_TRACK_NOMINAL_TRIM;
                        }
                    }
                }
                if((looper_record_active != 0U)
                        && (source_track == looper_record_track)
                        && (brick6_looper_runtime_is_overdub_recording(
                            looper_record_track) != 0U))
                {
                    for(uint32_t i = 0U; i < frames; ++i)
                    {
                        looper_record_l[i] += L[i];
                        looper_record_r[i] += R[i];
                    }
                }
                continue;
            }
            if((looper_record_active != 0U)
                    && (source_track < MIXER_MAX_TRACKS)
                    && (source_track != looper_record_track)
                    && (control_routing_audio_get_looper_source(looper_record_track, source_track) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    looper_record_l[i] += L[i];
                    looper_record_r[i] += R[i];
                }
            }

            if((audio_rec_bus_active != 0U)
                    && (source_track < BRICK_ENTITY_CAPACITY)
                    && ((audio_rec_projection.source_entity_mask
                        & (uint16_t)(1U << source_track)) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    audio_rec_bus_l[i] += L[i];
                    audio_rec_bus_r[i] += R[i];
                }
            }
        }

        if(send_bus_active != 0U)
        {
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                mt, lane_send_masks[t],
                (uint8_t)(lane_send_masks[t] & (uint8_t)~send_written_mask),
                (frames > 0U) ? (1.0f / (float)frames) : 0.0f);
            mixer_accumulate_send_plan_stereo(&send_plan, L, R,
                                              send_l, send_r, frames,
                                              MIXER_TRACK_NOMINAL_TRIM);
            mixer_finish_send_levels(mt);
            send_written_mask |= lane_send_masks[t];
        }

        if(route_main != 0U)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                dry_bus_l[i] += l_nom;
                dry_bus_r[i] += r_nom;
            }
        }
    }

    if (group_active != 0U)
    {
        mixer_audio_track_runtime_t *const group = &g_tracks[MIXER_GROUP_BUS_TRACK];
        const uint8_t group_static_flags =
            g_mixer_static_lane_flags[MIXER_GROUP_BUS_TRACK];

        /* AUDIO-owned GROUP order:
         * child dry and local A/B sends are post child-MIX.  A/B are processed
         * independently by the master's two kernels, reinjected into dry,
         * then master filter -> master MIX -> inserts -> global sends/dry.
         */
        for (uint8_t slot = 0U; slot < 2U; ++slot)
        {
            const uint8_t bit = (uint8_t)(1U << slot);
            if (((group_fx_written_mask & bit) != 0U)
                    && (audio_fx_runtime_process_parallel_slot(
                        BRICK_ENTITY_GROUP_MASTER_ID,
                        (audio_fx_slot_t)slot,
                        bus_group_fx_l[slot], bus_group_fx_r[slot],
                        frames) != 0U))
            {
                for (uint32_t i = 0U; i < frames; ++i)
                {
                    bus_group_l[i] += bus_group_fx_l[slot][i];
                    bus_group_r[i] += bus_group_fx_r[slot][i];
                }
            }
        }
        if (waveform_entity == BRICK_ENTITY_GROUP_MASTER_ID)
        {
            audio_waveform_capture_tap_reference_stereo_block(bus_group_l,
                                                               bus_group_r,
                                                               frames);
        }
        if ((group_static_flags & MIXER_STATIC_AUDIO_FX_PRE_FILTER) != 0U)
        {
            fx_chain_process_audio_fx_pre_filter_stereo(
                BRICK_ENTITY_GROUP_MASTER_ID,
                bus_group_l,
                bus_group_r,
                frames);
        }
        mixer_lane_run_stereo_path(MIXER_GROUP_BUS_TRACK,
                                   group,
                                   &g_track_filters[MIXER_GROUP_BUS_TRACK],
                                   bus_group_l,
                                   bus_group_r,
                                   frames);

        if ((group_static_flags & MIXER_STATIC_INSERT_STAGE) != 0U)
        {
            fx_chain_process_track_inserts_pre_fader(
                BRICK_ENTITY_GROUP_MASTER_ID,
                MIXER_GROUP_BUS_TRACK,
                group->insert_slot,
                MIXER_INSERTS_PER_TRACK,
                (uint8_t)((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_COMP) != 0U),
                bus_group_l,
                bus_group_r,
                frames);
        }

        float gain_cur = group->gain_current;
        float pan_cur = group->pan_current;
        float mute_gain_cur = group->mute_gain_current;
        const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
        const float gain_step = (group->gain - gain_cur) * inv_frames;
        const float pan_step = (group->pan - pan_cur) * inv_frames;
        const float mute_target = (group->mute != 0U) ? 0.0f : 1.0f;
        const float mute_step = 1.0f / 240.0f;
        const mixer_track_coefficient_plan_t coefficient_plan =
            mixer_prepare_track_coefficients(gain_cur,
                                              group->gain,
                                              pan_cur,
                                              group->pan,
                                              mute_gain_cur,
                                              mute_target);
        for (uint32_t i = 0U; i < frames; ++i)
        {
            float gain_l = 0.0f;
            float gain_r = 0.0f;
            mixer_track_coefficients_at(&coefficient_plan,
                                        gain_cur,
                                        pan_cur,
                                        mute_gain_cur,
                                        &gain_l,
                                        &gain_r);
            bus_group_l[i] *= gain_l;
            bus_group_r[i] *= gain_r;
            if (coefficient_plan.stable == 0U)
                mixer_advance_track_ramps(&gain_cur, gain_step,
                                          &pan_cur, pan_step,
                                          &mute_gain_cur, mute_target, mute_step);
        }
        group->gain_current = group->gain;
        group->pan_current = group->pan;
        group->mute_gain_current = mute_gain_cur;

        if (((group_static_flags & MIXER_STATIC_AUDIO_FX_ACTIVE) != 0U)
                && ((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_PRE_FILTER) == 0U)
                && ((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_COMP) == 0U))
        {
            fx_chain_process_audio_fx_post_fader(BRICK_ENTITY_GROUP_MASTER_ID,
                                                 bus_group_l,
                                                 bus_group_r,
                                                 frames);
        }
        if ((waveform_entity == BRICK_ENTITY_GROUP_MASTER_ID)
                && (audio_waveform_capture_needs_final_samples() != 0U))
        {
            audio_waveform_capture_tap_stereo_block(bus_group_l,
                                                   bus_group_r,
                                                   frames);
        }

        if (send_bus_active != 0U)
        {
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                group, group_send_mask,
                (uint8_t)(group_send_mask & (uint8_t)~send_written_mask),
                inv_frames);
            mixer_accumulate_send_plan_stereo(&send_plan,
                                              bus_group_l, bus_group_r,
                                              send_l, send_r, frames, 1.0f);
            mixer_finish_send_levels(group);
            send_written_mask |= group_send_mask;
        }

        if ((group_static_flags & MIXER_STATIC_ROUTE_MAIN) != 0U)
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += bus_group_l[i];
                bus_main_r[i] += bus_group_r[i];
            }
        }
    }

    if(looper_record_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * AUDIO_RECORDER_CHANNELS;
            looper_record_i32[out] = mixer_looper_float_to_pcm24(looper_record_l[i]);
            looper_record_i32[out + 1U] = mixer_looper_float_to_pcm24(looper_record_r[i]);
        }
        (void)brick6_looper_runtime_capture_from_irq(looper_record_track,
                                                     looper_record_i32,
                                                     frames);
    }

    mixer_external_inputs_clear();

    if(send_bus_active != 0U)
    {
        uint8_t zero_input_mask = tail_process_mask;
        if (delay_reverb_send_active != 0U)
            zero_input_mask |= MIXER_SEND_MASK_BIT(MIXER_REVERB_SEND_INDEX);
        const uint8_t clear_mask = (uint8_t)(
            zero_input_mask & (uint8_t)~send_written_mask);
        if (clear_mask != 0U)
            mixer_clear_send_buffers(clear_mask, send_l, send_r, frames);

        if(delay_active != 0U)
        {
            if(g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            {
                fx_delay_dual_global_process_block_add(send_l[MIXER_DELAY_SEND_INDEX],
                                                   send_r[MIXER_DELAY_SEND_INDEX],
                                                   bus_main_l,
                                                   bus_main_r,
                                                   (delay_reverb_send_active != 0U)
                                                       ? delay_reverb_l : NULL,
                                                   (delay_reverb_send_active != 0U)
                                                       ? delay_reverb_r : NULL,
                                                   frames);
            }
            else
            {
                fx_delay_stereo_global_process_block_add(send_l[MIXER_DELAY_SEND_INDEX],
                                                     send_r[MIXER_DELAY_SEND_INDEX],
                                                     bus_main_l,
                                                     bus_main_r,
                                                     (delay_reverb_send_active != 0U)
                                                         ? delay_reverb_l : NULL,
                                                     (delay_reverb_send_active != 0U)
                                                         ? delay_reverb_r : NULL,
                                                     frames);
            }
            if (delay_reverb_send_active != 0U)
            {
                for (uint32_t i = 0U; i < frames; ++i)
                {
                    send_l[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_l[i];
                    send_r[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_r[i];
                }
            }
        }

        if(reverb_active != 0U)
        {
            fx_reverb_global_process_block_add(
                send_l[MIXER_REVERB_SEND_INDEX],
                send_r[MIXER_REVERB_SEND_INDEX],
                bus_main_l,
                bus_main_r,
                frames);
        }

        if (modfx_active != 0U)
        {
            fx_modfx_global_process_block(send_l[MIXER_MODFX_SEND_INDEX],
                                          send_r[MIXER_MODFX_SEND_INDEX], frames);
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += send_l[MIXER_MODFX_SEND_INDEX][i];
                bus_main_r[i] += send_r[MIXER_MODFX_SEND_INDEX][i];
            }
        }

        const int8_t send_fx_slot = g_send_fx_slot[MIXER_REVERB_SEND_INDEX];
        if (send_fx_slot >= 0)
        {
            fx_chain_process_global_slot((uint32_t)send_fx_slot,
                                          send_l[MIXER_REVERB_SEND_INDEX],
                                          send_r[MIXER_REVERB_SEND_INDEX],
                                          frames);
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += send_l[MIXER_REVERB_SEND_INDEX][i];
                bus_main_r[i] += send_r[MIXER_REVERB_SEND_INDEX][i];
            }
        }
    }

    if(looper_xfade_apply_active != 0U)
    {
        if((mixer_looper_xfade_value_is_full(looper_xfade_start) != 0U)
                && (mixer_looper_xfade_value_is_full(looper_xfade_end) != 0U))
        {
            if((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
            {
                memcpy(bus_main_l, looper_bus_main_l, sizeof(float) * frames);
                memcpy(bus_main_r, looper_bus_main_r, sizeof(float) * frames);
            }
            else
            {
                memset(bus_main_l, 0, sizeof(float) * frames);
                memset(bus_main_r, 0, sizeof(float) * frames);
            }
        }
        else if(mixer_looper_xfade_values_are_stable(looper_xfade_start, looper_xfade_end) != 0U)
        {
            float xfade = looper_xfade_end;
            if(xfade < 0.0f)
            {
                xfade = 0.0f;
            }
            else if(xfade > 1.0f)
            {
                xfade = 1.0f;
            }

            const float live_gain = 1.0f - xfade;
            const float loop_gain = xfade;
            for(uint32_t i = 0U; i < frames; ++i)
            {
                const float loop_main_l = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_l[i]
                    : 0.0f;
                const float loop_main_r = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_r[i]
                    : 0.0f;
                bus_main_l[i] = (bus_main_l[i] * live_gain) + (loop_main_l * loop_gain);
                bus_main_r[i] = (bus_main_r[i] * live_gain) + (loop_main_r * loop_gain);
            }

        }
        else
        {
            const float xfade_step = (frames > 1U)
                ? ((looper_xfade_end - looper_xfade_start) / (float)(frames - 1U))
                : 0.0f;
            float xfade = (frames > 1U) ? looper_xfade_start : looper_xfade_end;
            for(uint32_t i = 0U; i < frames; ++i)
            {
                if(xfade < 0.0f)
                {
                    xfade = 0.0f;
                }
                else if(xfade > 1.0f)
                {
                    xfade = 1.0f;
                }

                const float live_gain = 1.0f - xfade;
                const float loop_main_l = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_l[i]
                    : 0.0f;
                const float loop_main_r = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_r[i]
                    : 0.0f;
                bus_main_l[i] = (bus_main_l[i] * live_gain) + (loop_main_l * xfade);
                bus_main_r[i] = (bus_main_r[i] * live_gain) + (loop_main_r * xfade);
                xfade += xfade_step;
            }
        }
    }

    /* One authoritative master dynamics slot, post returns/Looper crossfade. */
    fx_slot_t *const master_slot = fx_pool_get_slot(2U);
    if ((master_slot != NULL) && (master_slot->active != 0U)
            && (master_slot->state != NULL))
    {
        fx_chain_process_global_slot(2U, bus_main_l, bus_main_r, frames);
    }

    if((audio_rec_bus_active != 0U)
            && ((audio_rec_projection.source_flags
                & AUDIO_REC_BUS_SOURCE_LINE_DIRECT) != 0U))
    {
        const audio_physical_inputs_t *const inputs =
            audio_io_get_current_physical_inputs();
        for(uint32_t i = 0U; i < frames; ++i)
        {
            audio_rec_bus_l[i] += inputs->line.left[i];
            audio_rec_bus_r[i] += inputs->line.right[i];
        }
    }

    uint32_t audio_rec_peak_abs_pcm24 = 0U;
    if(audio_rec_bus_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * AUDIO_RECORDER_CHANNELS;
            const int32_t pcm_l = mixer_looper_float_to_pcm24(audio_rec_bus_l[i]);
            const int32_t pcm_r = mixer_looper_float_to_pcm24(audio_rec_bus_r[i]);
            const uint32_t abs_l = (pcm_l < 0) ? (uint32_t)(-pcm_l) : (uint32_t)pcm_l;
            const uint32_t abs_r = (pcm_r < 0) ? (uint32_t)(-pcm_r) : (uint32_t)pcm_r;
            if(abs_l > audio_rec_peak_abs_pcm24)
            {
                audio_rec_peak_abs_pcm24 = abs_l;
            }
            if(abs_r > audio_rec_peak_abs_pcm24)
            {
                audio_rec_peak_abs_pcm24 = abs_r;
            }
            if(audio_rec_capture_active != 0U)
            {
                audio_rec_bus_i32[out] = pcm_l;
                audio_rec_bus_i32[out + 1U] = pcm_r;
            }
        }
        if(audio_rec_capture_active != 0U)
        {
            (void)sample_capture_push_audio_block_from_irq(audio_rec_bus_i32, frames);
        }
    }
    audio_rec_level_snapshot_audio_publish(audio_rec_peak_abs_pcm24);

    if(track_count > 0U)
    {
        memcpy(tracks[0].L, bus_main_l, sizeof(float) * frames);
        memcpy(tracks[0].R, bus_main_r, sizeof(float) * frames);
    }

}
