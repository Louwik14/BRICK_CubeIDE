static uint8_t mod_matrix_audio_slew_direct_cycle(uint8_t track, uint8_t op)
{
    if ((track >= SEQ_TRACK_COUNT) || (op >= 2U))
    {
        return 1U;
    }
    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    const uint8_t src = config->slew_source[op];
    const uint8_t self = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1
                                    : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
    const uint8_t other = (op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW2
                                     : (uint8_t)MOD_MATRIX_SOURCE_SLEW1;
    if (src == self)
        return 1U;
    if (src == other)
    {
        if (config->slew_source[1U - op] == self)
            return 1U;
    }
    return 0U;
}

static uint8_t mod_matrix_track_has_operator_route(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    const uint16_t mask = g_mod_matrix_route_cache[track].required_source_mask;
    return ((mask & ((uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW1)
                    | (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_SLEW2))) != 0U) ? 1U : 0U;
}

static uint8_t mod_matrix_get_operator_source_value(const mod_matrix_operator_runtime_t *rt,
                                                    const float source_values[MOD_MATRIX_SOURCE_COUNT],
                                                    const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                                    uint8_t source,
                                                    float *out)
{
    if ((out == NULL) || (source >= (uint8_t)MOD_MATRIX_SOURCE_COUNT))
    {
        return 0U;
    }

    switch ((mod_matrix_source_t)source)
    {
        case MOD_MATRIX_SOURCE_MULTI1:
        case MOD_MATRIX_SOURCE_MULTI2:
        {
            const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_MULTI1) ? 0U : 1U;
            if ((rt == NULL) || (rt->multi_valid[op] == 0U))
            {
                return 0U;
            }
            *out = rt->multi[op];
            return 1U;
        }
        case MOD_MATRIX_SOURCE_SLEW1:
        case MOD_MATRIX_SOURCE_SLEW2:
        {
            const uint8_t op = (source == (uint8_t)MOD_MATRIX_SOURCE_SLEW1) ? 0U : 1U;
            if ((rt == NULL) || (rt->slew_valid[op] == 0U))
            {
                return 0U;
            }
            *out = rt->slew[op];
            return 1U;
        }
        default:
            if ((source_valid == NULL) || (source_values == NULL) || (source_valid[source] == 0U))
            {
                return 0U;
            }
            *out = source_values[source];
            return 1U;
    }
}

void mod_matrix_process_operators_ramped(uint8_t track,
                                         float source_start[MOD_MATRIX_SOURCE_COUNT],
                                         float source_end[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                         uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                         uint32_t elapsed_frames)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (source_start == NULL)
            || (source_end == NULL)
            || (source_valid == NULL)
            || (source_discontinuous == NULL))
    {
        return;
    }
    if (mod_matrix_track_has_operator_route(track) == 0U)
    {
        return;
    }

    const mod_matrix_operator_config_t *const config =
        &g_mod_matrix_operator_config[track];
    mod_matrix_operator_runtime_t *const rt = &g_mod_matrix_operator_runtime[track];
    const uint16_t required_mask =
        g_mod_matrix_route_cache[track].required_source_mask;

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t output = (op == 0U)
            ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1
            : (uint8_t)MOD_MATRIX_SOURCE_MULTI2;
        if ((required_mask & (uint16_t)(1U << output)) == 0U)
        {
            rt->multi_valid[op] = 0U;
            source_valid[output] = 0U;
            source_discontinuous[output] = 0U;
            continue;
        }
        const uint8_t src_a = config->multi_source[op][0];
        const uint8_t src_b = config->multi_source[op][1];
        float a_start = 0.0f;
        float b_start = 0.0f;
        float a_end = 0.0f;
        float b_end = 0.0f;
        const uint8_t invalid_src = (uint8_t)((src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_a == (uint8_t)MOD_MATRIX_SOURCE_MULTI2)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI1)
                                             || (src_b == (uint8_t)MOD_MATRIX_SOURCE_MULTI2));
        if ((invalid_src == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src_a, &a_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src_b, &b_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src_a, &a_end) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src_b, &b_end) != 0U))
        {
            const float start = mod_matrix_clampf(a_start * b_start, -1.0f, 1.0f);
            const float end = mod_matrix_clampf(a_end * b_end, -1.0f, 1.0f);
            rt->multi[op] = end;
            rt->multi_valid[op] = 1U;
            source_start[output] = start;
            source_end[output] = end;
            source_valid[output] = 1U;
            source_discontinuous[output] = (uint8_t)(source_discontinuous[src_a]
                || source_discontinuous[src_b]);
        }
        else
        {
            rt->multi_valid[op] = 0U;
            source_discontinuous[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_MULTI1 : (uint8_t)MOD_MATRIX_SOURCE_MULTI2] = 0U;
        }
    }

    for (uint8_t op = 0U; op < 2U; ++op)
    {
        const uint8_t output = (op == 0U)
            ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1
            : (uint8_t)MOD_MATRIX_SOURCE_SLEW2;
        if ((required_mask & (uint16_t)(1U << output)) == 0U)
        {
            rt->slew_valid[op] = 0U;
            source_valid[output] = 0U;
            source_discontinuous[output] = 0U;
            continue;
        }
        const uint8_t src = config->slew_source[op];
        float input_start = 0.0f;
        float input_end = 0.0f;
        if ((mod_matrix_audio_slew_direct_cycle(track, op) == 0U)
                && (mod_matrix_get_operator_source_value(rt, source_start, source_valid, src, &input_start) != 0U)
                && (mod_matrix_get_operator_source_value(rt, source_end, source_valid, src, &input_end) != 0U))
        {
            const float amount = mod_matrix_clampf(config->slew_amount[op], 0.0f, 1.0f);
            const float tau_frames = 16.0f + (amount * amount * 48000.0f);
            const float elapsed = (elapsed_frames == 0U) ? 1.0f : (float)elapsed_frames;
            const float coeff = (amount <= 0.0f) ? 1.0f : (elapsed / (tau_frames + elapsed));
            float start = input_start;
            if (rt->slew_valid[op] == 0U)
            {
                rt->slew[op] = input_start;
            }
            else
            {
                start = rt->slew[op];
            }
            const float end = mod_matrix_clampf(start + (input_end - start) * coeff, -1.0f, 1.0f);
            rt->slew[op] = end;
            rt->slew_valid[op] = 1U;
            source_start[output] = start;
            source_end[output] = end;
            source_valid[output] = 1U;
            source_discontinuous[output] = source_discontinuous[src];
        }
        else
        {
            rt->slew_valid[op] = 0U;
            source_discontinuous[(op == 0U) ? (uint8_t)MOD_MATRIX_SOURCE_SLEW1 : (uint8_t)MOD_MATRIX_SOURCE_SLEW2] = 0U;
        }
    }
}

void mod_matrix_process_operators(uint8_t track,
                                  float source_values[MOD_MATRIX_SOURCE_COUNT],
                                  uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                  uint32_t elapsed_frames)
{
    if ((source_values == NULL) || (source_valid == NULL))
    {
        return;
    }

    float source_end[MOD_MATRIX_SOURCE_COUNT];
    uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};
    memcpy(source_end, source_values, sizeof(source_end));
    mod_matrix_process_operators_ramped(track,
                                        source_values,
                                        source_end,
                                        source_valid,
                                        source_discontinuous,
                                        elapsed_frames);
}

void mod_matrix_process_track_ramped(uint8_t track,
                                     const track_audio_runtime_ctx_t *ctx,
                                     const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                     const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT],
                                     uint32_t elapsed_frames)
{
    (void)ctx;
    if ((track >= SEQ_TRACK_COUNT)
            || (source_start == NULL)
            || (source_end == NULL)
            || (source_valid == NULL)
            || (source_discontinuous == NULL))
    {
        return;
    }
    const mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    if (plan->route_count == 0U)
    {
        return;
    }

    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    uint8_t touched[MOD_MATRIX_SLOT_COUNT] = {0};
    uint16_t discontinuity_source_mask = 0U;

    for (uint8_t source = 0U; source < MOD_MATRIX_SOURCE_COUNT; ++source)
    {
        if (source_discontinuous[source] != 0U)
        {
            discontinuity_source_mask |= (uint16_t)(1U << source);
        }
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const uint8_t runtime_index = plan->destinations[i].runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }
        rt->destinations[runtime_index].sum = 0.0f;
        rt->destinations[runtime_index].sum_end = 0.0f;
    }

    for (uint8_t i = 0U; i < plan->route_count; ++i)
    {
        const mod_matrix_track_route_t *const route = &plan->routes[i];
        if ((route->source >= MOD_MATRIX_SOURCE_COUNT)
                || (source_valid[route->source] == 0U)
                || (route->destination_index >= plan->destination_count))
        {
            continue;
        }

        const uint8_t runtime_index = plan->destinations[route->destination_index].runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        if (dst->base_valid == 0U)
            continue;
        touched[route->destination_index] = 1U;
        dst->modulation_active = 1U;
        dst->sum += source_start[route->source] * route->scale;
        dst->sum_end += source_end[route->source] * route->scale;
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_track_destination_t *const planned = &plan->destinations[i];
        const uint8_t runtime_index = planned->runtime_destination_index;
        if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        if (touched[i] == 0U)
        {
            if (dst->modulation_active != 0U)
            {
                mod_matrix_restore_destination_value(dst);
                dst->modulation_active = 0U;
            }
            dst->sum = 0.0f;
            dst->sum_end = 0.0f;
            dst->ramp = (mod_destination_ramp_t){0};
            continue;
        }

        const float start = mod_matrix_clampf(dst->base_value + dst->sum,
                                              dst->min_value,
                                              dst->max_value);
        const float end = mod_matrix_clampf(dst->base_value + dst->sum_end,
                                            dst->min_value,
                                            dst->max_value);
        const uint8_t discontinuous =
            ((planned->discontinuity_source_mask & discontinuity_source_mask) != 0U) ? 1U : 0U;
        mod_destination_ramp_prepare(start,
                                     end,
                                     elapsed_frames,
                                     discontinuous,
                                     &dst->ramp);
        (void)mod_destination_catalog_apply_ramp_prepared(
            &dst->prepared, &dst->ramp);
    }
}

void mod_matrix_process_track(uint8_t track,
                              const track_audio_runtime_ctx_t *ctx,
                              const float source_values[MOD_MATRIX_SOURCE_COUNT],
                              const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT])
{
    if ((source_values == NULL) || (source_valid == NULL))
    {
        return;
    }

    float source_end[MOD_MATRIX_SOURCE_COUNT];
    uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};
    memcpy(source_end, source_values, sizeof(source_end));
    mod_matrix_process_track_ramped(track,
                                    ctx,
                                    source_values,
                                    source_end,
                                    source_valid,
                                    source_discontinuous,
                                    1U);
}

void mod_matrix_process_poly_voice_ramped(uint8_t track,
                                          uint8_t voice_slot,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const float source_start[MOD_MATRIX_SOURCE_COUNT],
                                          const float source_end[MOD_MATRIX_SOURCE_COUNT],
                                          const uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT])
{
    (void)ctx;
    if ((track >= SEQ_TRACK_COUNT) || (source_start == NULL)
            || (source_end == NULL) || (source_valid == NULL))
    {
        return;
    }

    const mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    if (plan->route_count == 0U) return;
    float sums[MOD_MATRIX_SLOT_COUNT] = {0.0f};

    for (uint8_t i = 0U; i < plan->route_count; ++i)
    {
        const mod_matrix_poly_route_t *const route = &plan->routes[i];
        if (source_valid[route->source] != 0U)
            sums[route->destination_index] += source_end[route->source] * route->scale;
    }

    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_poly_destination_t *const planned = &plan->destinations[i];
        const uint8_t runtime_index = planned->runtime_destination_index;
        const mod_matrix_runtime_destination_t *const destination =
            &g_mod_matrix_runtime[track].destinations[runtime_index];
        if (destination->base_valid == 0U)
            continue;
        const float value = mod_matrix_clampf(destination->base_value + sums[i],
                                              destination->min_value,
                                              destination->max_value);
        (void)mod_destination_catalog_apply_poly_prepared(
            &destination->prepared, voice_slot, value);
    }
}

void mod_matrix_reset_poly_voice(uint8_t track,
                                 uint8_t voice_slot,
                                 const track_audio_runtime_ctx_t *ctx)
{
    (void)ctx;
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    const mod_matrix_poly_plan_t *const plan = &g_mod_matrix_poly_plan[track];
    for (uint8_t i = 0U; i < plan->destination_count; ++i)
    {
        const mod_matrix_poly_destination_t *const destination = &plan->destinations[i];
        const uint8_t runtime_index = destination->runtime_destination_index;
        const mod_matrix_runtime_destination_t *const runtime_destination =
            &g_mod_matrix_runtime[track].destinations[runtime_index];
        if (runtime_destination->base_valid == 0U)
            continue;
        (void)mod_destination_catalog_apply_poly_prepared(
            &runtime_destination->prepared, voice_slot,
            runtime_destination->base_value);
    }
}

uint8_t mod_matrix_get_destination_ramp(uint8_t track,
                                        param_id_t destination,
                                        mod_destination_ramp_t *out_ramp)
{
    if ((track >= SEQ_TRACK_COUNT) || (destination >= PARAM_COUNT) || (out_ramp == NULL))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_matrix_audio_resolve_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    const mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner],
            mod_destination_address_make(track, destination));
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return 0U;
    }

    *out_ramp = dst->ramp;
    return 1U;
}

void audio_mod_matrix_base_update(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    audio_mod_destination_catalog_invalidate_runtime_value(track, id);
    uint8_t owner = 0U;
    if (mod_matrix_audio_resolve_owner(track, &owner) == 0U) return;
    const mod_destination_address_t address = mod_destination_address_make(track, id);
    mod_matrix_runtime_destination_t *const dst =
        mod_matrix_find_runtime_destination(&g_mod_matrix_runtime[owner], address);
    if (dst != NULL)
    {
        dst->base_value = value;
        dst->base_valid = 1U;
    }
}
