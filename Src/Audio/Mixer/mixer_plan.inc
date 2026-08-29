void mixer_rebuild_static_plan(void)
{
    for (uint8_t lane = 0U; lane < MIXER_MAX_TRACKS; ++lane)
    {
        uint8_t flags = (g_tracks[lane].route_master != 0U)
            ? MIXER_STATIC_ROUTE_MAIN : 0U;
        const brick_entity_id_t entity =
            audio_note_engine_adapter_entity_for_mix_lane(lane);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (entity < BRICK_ENTITY_CAPACITY)
                && (audio_note_engine_adapter_current_ctx(
                    entity, &ctx_value) != 0U) ? &ctx_value : NULL;
        if ((ctx != NULL)
                && ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) != 0U))
            flags |= MIXER_STATIC_GROUP_CHILD;
        if ((ctx != NULL)
                && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
            flags |= MIXER_STATIC_LOOPER;
        if ((entity < BRICK_ENTITY_CAPACITY)
                && ((ctx == NULL)
                    || ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) == 0U))
                && (fx_chain_audio_fx_is_active(entity) != 0U))
        {
            flags |= MIXER_STATIC_AUDIO_FX_ACTIVE;
            if (audio_fx_runtime_get_filter_pos(entity)
                    != AUDIO_FX_FILTER_POS_PRE)
                flags |= MIXER_STATIC_AUDIO_FX_PRE_FILTER;
        }
        if ((entity < BRICK_ENTITY_CAPACITY)
                && (fx_chain_audio_fx_is_comp(entity) != 0U))
            flags |= MIXER_STATIC_AUDIO_FX_COMP;
        if (fx_chain_track_inserts_require_stereo(
                entity,
                g_tracks[lane].insert_slot,
                MIXER_INSERTS_PER_TRACK) != 0U)
            flags |= MIXER_STATIC_REQUIRES_STEREO;
        if (fx_chain_track_has_pre_fader_insert(
                entity,
                g_tracks[lane].insert_slot,
                MIXER_INSERTS_PER_TRACK) != 0U)
            flags |= MIXER_STATIC_INSERT_STAGE;
        g_mixer_static_lane_flags[lane] = flags;
    }

    track_audio_runtime_ctx_t group_master_ctx_value;
    const track_audio_runtime_ctx_t *const group_master_ctx =
        (audio_note_engine_adapter_current_ctx(
            BRICK_ENTITY_GROUP_MASTER_ID, &group_master_ctx_value) != 0U)
            ? &group_master_ctx_value : NULL;
    g_mixer_static_group_active = (uint8_t)((group_master_ctx != NULL)
        && ((group_master_ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U));
}

uint8_t mixer_audio_set_route(uint8_t track, uint32_t route)
{
    if (track >= MIXER_MAX_TRACKS) return 0U;
    g_tracks[track].route_master = ((route & MIXER_ROUTE_MASTER) != 0U);
    mixer_rebuild_static_plan();
    return 1U;
}

uint8_t mixer_audio_set_insert_slot(uint8_t track, uint8_t insert, int8_t slot)
{
    if ((track >= MIXER_MAX_TRACKS) || (insert >= MIXER_INSERTS_PER_TRACK))
        return 0U;
    g_tracks[track].insert_slot[insert] = slot;
    mixer_rebuild_static_plan();
    return 1U;
}

static void mixer_track_filter_process_biquad_stereo_block(mixer_track_filter_t *filter,
                                                           float *left,
                                                           float *right,
                                                           uint32_t frames,
                                                           float cutoff_start_hz,
                                                           float cutoff_mod_start_hz,
                                                           float resonance_start,
                                                           float keytrack_ratio_start);
static uint8_t ITCM_TEXT mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
                                                     float *mono,
                                                     uint32_t frames,
                                                     const mixer_poly_cutoff_override_t *poly_cutoff);
