void audio_mod_matrix_init(void)
{
    memset(g_mod_matrix_runtime, 0, sizeof(g_mod_matrix_runtime));
    memset(g_mod_matrix_operator_config, 0, sizeof(g_mod_matrix_operator_config));
    memset(g_mod_matrix_route_cache, 0, sizeof(g_mod_matrix_route_cache));
    memset(g_mod_matrix_track_plan, 0, sizeof(g_mod_matrix_track_plan));
    memset(g_mod_matrix_poly_plan, 0, sizeof(g_mod_matrix_poly_plan));
    memset(g_mod_matrix_operator_runtime, 0, sizeof(g_mod_matrix_operator_runtime));
    memset(g_mod_matrix_audio_state, 0, sizeof(g_mod_matrix_audio_state));
    g_mod_matrix_audio_dirty_mask = 0U;
    g_mod_matrix_any_route = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t selected = 0U;
        mod_matrix_audio_set_defaults(g_mod_matrix_audio_state[track].slots, &selected);
        g_mod_matrix_audio_state[track].multi_source[0][0] =
            (uint8_t)param_spec[PARAM_MOD_MULTI_1_A].default_value;
        g_mod_matrix_audio_state[track].multi_source[0][1] =
            (uint8_t)param_spec[PARAM_MOD_MULTI_1_B].default_value;
        g_mod_matrix_audio_state[track].multi_source[1][0] =
            (uint8_t)param_spec[PARAM_MOD_MULTI_2_A].default_value;
        g_mod_matrix_audio_state[track].multi_source[1][1] =
            (uint8_t)param_spec[PARAM_MOD_MULTI_2_B].default_value;
        g_mod_matrix_audio_state[track].slew_source[0] =
            (uint8_t)param_spec[PARAM_MOD_SLEW_1_SOURCE].default_value;
        g_mod_matrix_audio_state[track].slew_source[1] =
            (uint8_t)param_spec[PARAM_MOD_SLEW_2_SOURCE].default_value;
        g_mod_matrix_audio_state[track].slew_amount[0] =
            param_spec[PARAM_MOD_SLEW_1_AMOUNT].default_value;
        g_mod_matrix_audio_state[track].slew_amount[1] =
            param_spec[PARAM_MOD_SLEW_2_AMOUNT].default_value;
        for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
        {
            g_mod_matrix_runtime[track].destinations[i].destination = (uint16_t)MOD_DESTINATION_NONE;
            g_mod_matrix_runtime[track].destinations[i].max_value = 127.0f;
        }
    }
    audio_mod_destination_catalog_reset_runtime();
}

static uint16_t mod_matrix_required_mask_expand(uint8_t track,
                                                uint8_t source,
                                                uint8_t depth)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_NONE)
            || (source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT)
            || (depth >= 4U))
        return 0U;

    const uint16_t own = (uint16_t)(1U << source);
    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    if ((source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI2))
    {
        const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1) ? 0U : 1U;
        const uint8_t a = config->multi_source[op][0];
        const uint8_t b = config->multi_source[op][1];
        if ((a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                || (a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                || (b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                || (b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2))
            return own;
        return (uint16_t)(own
            | mod_matrix_required_mask_expand(track, a, (uint8_t)(depth + 1U))
            | mod_matrix_required_mask_expand(track, b, (uint8_t)(depth + 1U)));
    }
    if ((source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1)
            || (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW2))
    {
        const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1) ? 0U : 1U;
        const uint8_t input = config->slew_source[op];
        const uint8_t self = source;
        const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2
                                         : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
        if ((input == self)
                || ((input == other) && (config->slew_source[1U - op] == self)))
            return own;
        return (uint16_t)(own | mod_matrix_required_mask_expand(
            track, input, (uint8_t)(depth + 1U)));
    }
    return own;
}

static void mod_matrix_audio_rebuild_route_cache_track(
    uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    memset(plan, 0, sizeof(*plan));
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    const track_family_t family = mod_matrix_ui_family_from_ctx(ctx);
    const track_type_t type = mod_matrix_ui_type_from_ctx(ctx);
    const mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
    mod_matrix_rebuild_track_plan(track, family, type, ctx, state);
    if (ctx != NULL)
    {
        for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
        {
            const track_mod_matrix_slot_t *const s = &state->slots[slot];
            uint8_t target = 0U;
            param_id_t destination = PARAM_COUNT;
            if ((mod_matrix_slot_is_effective(track, s, family, type, ctx) == 0U)
                    || (mod_destination_address_resolve(
                        s->destination, &target, &destination) == 0U)
                    || (target != track)
                    || (s->source < (uint8_t)MOD_MATRIX_SOURCE_LFO1)
                    || (s->source > (uint8_t)MOD_MATRIX_SOURCE_LFO3)) continue;
            const uint8_t lfo = (uint8_t)(s->source - (uint8_t)MOD_MATRIX_SOURCE_LFO1);
            const mod_lfo_trig_mode_t trig = mod_lfo_v1_effective_trig(track, lfo);
            if ((trig < MOD_LFO_TRIG_POLY_TRIG)
                    || (mod_destination_catalog_poly_voice_supported(
                        destination, ctx) == 0U)) continue;

            uint8_t dst_index = plan->destination_count;
            for (uint8_t i = 0U; i < plan->destination_count; ++i)
            {
                const uint8_t runtime_index =
                    plan->destinations[i].runtime_destination_index;
                if ((runtime_index < MOD_MATRIX_SLOT_COUNT)
                        && (g_mod_matrix_runtime[track].destinations[runtime_index].destination
                            == s->destination))
                {
                    dst_index = i;
                    break;
                }
            }
            if (dst_index == plan->destination_count)
            {
                mod_matrix_runtime_destination_t *const runtime_destination =
                    mod_matrix_find_runtime_destination(
                        &g_mod_matrix_runtime[track], s->destination);
                if (runtime_destination == NULL)
                    continue;
                const uint8_t runtime_index = (uint8_t)(runtime_destination
                    - &g_mod_matrix_runtime[track].destinations[0]);
                if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
                    continue;
                plan->destinations[dst_index].runtime_destination_index = runtime_index;
                plan->destination_count++;
            }
            const uint8_t runtime_index =
                plan->destinations[dst_index].runtime_destination_index;
            mod_matrix_poly_route_t *const route = &plan->routes[plan->route_count++];
            route->source = s->source;
            route->destination_index = dst_index;
            route->scale = (s->depth / 127.0f)
                * (g_mod_matrix_runtime[track].destinations[runtime_index].max_value
                    - g_mod_matrix_runtime[track].destinations[runtime_index].min_value);
            plan->source_mask |= (uint8_t)(1U << lfo);
        }
    }
    uint16_t required_source_mask = 0U;
    const mod_matrix_track_plan_t *const track_plan = &g_mod_matrix_track_plan[track];
    for (uint8_t route = 0U; route < track_plan->route_count; ++route)
        required_source_mask |= mod_matrix_required_mask_expand(
            track, track_plan->routes[route].source, 0U);
    g_mod_matrix_route_cache[track].required_source_mask = required_source_mask;
    g_mod_matrix_route_cache[track].any_route =
        (track_plan->route_count != 0U) ? 1U : 0U;
    mixer_invalidate_external_poly_track(track);
    mod_matrix_recompute_global_route_flag();
}

uint8_t audio_mod_matrix_apply_param(uint8_t track, uint8_t slot,
                                     param_id_t id, float value)
{
    if (track >= SEQ_TRACK_COUNT) return 0U;
    mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
    if (slot < MOD_MATRIX_SLOT_COUNT)
    {
        track_mod_matrix_slot_t *const s = &state->slots[slot];
        if (id == PARAM_MOD_MATRIX_SOURCE)
            s->source = (uint8_t)mod_matrix_clampf(value, 0.0f,
                (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
        else if (id == PARAM_MOD_MATRIX_DEST)
            s->destination = (mod_destination_address_t)value;
        else if (id == PARAM_MOD_MATRIX_DEPTH)
            s->depth = mod_matrix_clampf(value, -127.0f, 127.0f);
        else if (id == PARAM_MOD_MATRIX_SLOT)
            s->enabled = (value >= 0.5f) ? 1U : 0U;
        else return 0U;
    }
    else
    {
        uint8_t op = 0U;
        if (id == PARAM_MOD_MULTI_1_A) state->multi_source[0][0] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_1_B) state->multi_source[0][1] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_2_A) state->multi_source[1][0] = (uint8_t)value;
        else if (id == PARAM_MOD_MULTI_2_B) state->multi_source[1][1] = (uint8_t)value;
        else if ((id == PARAM_MOD_SLEW_1_SOURCE)
                || (id == PARAM_MOD_SLEW_2_SOURCE))
        {
            op = (id == PARAM_MOD_SLEW_2_SOURCE) ? 1U : 0U;
            const uint8_t source = (uint8_t)value;
            if (state->slew_source[op] != source)
            {
                state->slew_source[op] = source;
                g_mod_matrix_operator_runtime[track].slew[op] = 0.0f;
                g_mod_matrix_operator_runtime[track].slew_valid[op] = 0U;
            }
        }
        else if (id == PARAM_MOD_SLEW_1_AMOUNT) state->slew_amount[0] = value;
        else if (id == PARAM_MOD_SLEW_2_AMOUNT) state->slew_amount[1] = value;
        else return 0U;
    }
    g_mod_matrix_audio_dirty_mask |= (uint16_t)(1U << track);
    return 1U;
}

void audio_mod_matrix_rebuild_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT) return;
    for (uint8_t owner = 0U; owner < SEQ_TRACK_COUNT; ++owner)
    {
        uint8_t depends = (owner == track) ? 1U : 0U;
        for (uint8_t slot = 0U; (slot < MOD_MATRIX_SLOT_COUNT) && !depends; ++slot)
        {
            uint8_t target = 0U; param_id_t destination = PARAM_COUNT;
            const track_mod_matrix_slot_t *const s =
                &g_mod_matrix_audio_state[owner].slots[slot];
            if (s->enabled && mod_destination_address_resolve(
                    s->destination, &target, &destination) && (target == track))
                depends = 1U;
        }
        if (depends) g_mod_matrix_audio_dirty_mask |= (uint16_t)(1U << owner);
    }
}

void audio_mod_matrix_finalize_dirty(void)
{
    const uint16_t dirty = g_mod_matrix_audio_dirty_mask;
    g_mod_matrix_audio_dirty_mask = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if ((dirty & (uint16_t)(1U << track)) == 0U) continue;
        mod_matrix_operator_config_t *const config = &g_mod_matrix_operator_config[track];
        const mod_matrix_audio_state_t *const state = &g_mod_matrix_audio_state[track];
        for (uint8_t op = 0U; op < 2U; ++op)
        {
            config->multi_source[op][0] = state->multi_source[op][0];
            config->multi_source[op][1] = state->multi_source[op][1];
            config->slew_source[op] = state->slew_source[op];
            config->slew_amount[op] = state->slew_amount[op];
        }
        track_audio_runtime_ctx_t ctx;
        config->drum_md_slot_count = 0U;
        if ((audio_note_engine_adapter_current_ctx(track, &ctx) != 0U)
                && (ctx.type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD))
            config->drum_md_slot_count = md_model_profile_get(
                drum_synth_get_md_model_for_instance(
                    ctx.program_route.instance_id))->slot_count;
        mod_matrix_audio_rebuild_route_cache_track(track);
    }
}

