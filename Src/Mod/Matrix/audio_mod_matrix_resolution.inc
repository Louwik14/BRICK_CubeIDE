static uint8_t mod_matrix_audio_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    track_audio_runtime_ctx_t ctx;
    if ((out_owner == NULL) || !audio_note_engine_adapter_current_ctx(track, &ctx))
        return 0U;
    if ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) == 0U)
    { *out_owner = track; return 1U; }
    for (uint8_t entity = 0U; entity < SEQ_TRACK_COUNT; ++entity)
        if (audio_note_engine_adapter_current_ctx(entity, &ctx)
                && ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U))
        { *out_owner = entity; return 1U; }
    return 0U;
}

/* Read the model authorities that the AUDIO engines actually execute.  This is
 * a short-lived projection used while building a Matrix plan, never Matrix
 * state. */
static void mod_matrix_audio_read_models(
    uint8_t track, const track_audio_runtime_ctx_t *ctx,
    mod_destination_audio_models_t *out)
{
    memset(out, 0, sizeof(*out));
    out->audio_fx_model[0] = audio_fx_runtime_get_model(track, AUDIO_FX_SLOT_A);
    out->audio_fx_model[1] = audio_fx_runtime_get_model(track, AUDIO_FX_SLOT_B);
    if ((ctx == NULL) || (ctx->program_route.active == 0U)) return;
    const uint8_t instance = ctx->program_route.instance_id;
    if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
        for (uint8_t osc = 0U; osc < 2U; ++osc)
            out->prism_model[osc] = brick6_braids_runtime_get_osc_model(instance, osc);
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
        for (uint8_t slot = 0U; slot < 3U; ++slot)
            out->stack_model[slot] = (uint8_t)brick6_stack_runtime_get_slot_model(instance, slot);
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM
             && ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD)
        out->drum_md_slot_count = md_model_profile_get(
            drum_synth_get_md_model_for_instance(instance))->slot_count;
}


static ui_track_family_t mod_matrix_ui_family_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_FAMILY_OFF;
    }

    switch ((track_runtime_family_t)ctx->family)
    {
        case TRACK_RUNTIME_FAMILY_SYNTH:
            return UI_TRACK_FAMILY_SYNTH;
        case TRACK_RUNTIME_FAMILY_SAMPLER:
            return UI_TRACK_FAMILY_SAMPLER;
        case TRACK_RUNTIME_FAMILY_DRUM:
            return UI_TRACK_FAMILY_DRUM;
            return UI_TRACK_FAMILY_OFF;
        case TRACK_RUNTIME_FAMILY_MIDI:
            return UI_TRACK_FAMILY_MIDI;
        case TRACK_RUNTIME_FAMILY_EXTERNAL:
            return UI_TRACK_FAMILY_EXTERNAL;
        case TRACK_RUNTIME_FAMILY_OFF:
        default:
            return UI_TRACK_FAMILY_OFF;
    }
}

static ui_track_type_t mod_matrix_ui_type_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_TYPE_NONE;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_RAM:
            return UI_TRACK_TYPE_RAM;
        case TRACK_RUNTIME_TYPE_PRISM:
            return UI_TRACK_TYPE_PRISM;
        case TRACK_RUNTIME_TYPE_WAVE:
            return UI_TRACK_TYPE_WAVE;
        case TRACK_RUNTIME_TYPE_STACK:
            return UI_TRACK_TYPE_STACK;
        case TRACK_RUNTIME_TYPE_DRUM_MD:
            return UI_TRACK_TYPE_DRUM_MD;
        case TRACK_RUNTIME_TYPE_MIDI:
            return UI_TRACK_TYPE_MIDI;
        case TRACK_RUNTIME_TYPE_EXTERNAL:
            return UI_TRACK_TYPE_EXTERNAL;
        case TRACK_RUNTIME_TYPE_STREAM:
            return UI_TRACK_TYPE_STREAM;
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return UI_TRACK_TYPE_DRUM_BD_ANALOG;
        case TRACK_RUNTIME_TYPE_LOOPER:
            return UI_TRACK_TYPE_LOOPER;
        case TRACK_RUNTIME_TYPE_MULTI:
            return UI_TRACK_TYPE_MULTI;
        case TRACK_RUNTIME_TYPE_NONE:
        default:
            return UI_TRACK_TYPE_NONE;
    }
}

static uint8_t mod_matrix_slot_is_effective(uint8_t track,
                                            const track_mod_matrix_slot_t *slot,
                                            ui_track_family_t family,
                                            ui_track_type_t type,
                                            const track_audio_runtime_ctx_t *ctx)
{
    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    const uint8_t is_group_master = (uint8_t)((ctx != NULL)
        && ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U));
    if ((slot == NULL)
            || (slot->enabled == 0U)
            || (slot->source == (uint8_t)MOD_MATRIX_SOURCE_NONE)
            || (slot->source >= MOD_MATRIX_SOURCE_COUNT)
            || (mod_destination_address_resolve(slot->destination,
                                                &target, &destination) == 0U)
            || (slot->depth == 0.0f))
    {
        return 0U;
    }

    switch ((mod_matrix_source_t)slot->source)
    {
        case MOD_MATRIX_SOURCE_LFO1:
        case MOD_MATRIX_SOURCE_LFO2:
        case MOD_MATRIX_SOURCE_LFO3:
        case MOD_MATRIX_SOURCE_ENV3:
        case MOD_MATRIX_SOURCE_MULTI1:
        case MOD_MATRIX_SOURCE_MULTI2:
        case MOD_MATRIX_SOURCE_SLEW1:
        case MOD_MATRIX_SOURCE_SLEW2:
            break;

        case MOD_MATRIX_SOURCE_ENV_VCA:
            if (is_group_master != 0U)
            {
                return 0U;
            }
            if ((ctx == NULL)
                    || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS)
                    || (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
            {
                return 0U;
            }
            break;

        case MOD_MATRIX_SOURCE_ENV_FLT:
            if (is_group_master != 0U)
            {
                return 0U;
            }
            if ((ctx == NULL)
                    || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
                    || ((ctx->flags & CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER) == 0U)
                    || (ctx->has_filter_target == 0U))
            {
                return 0U;
            }
            break;

        default:
            return 0U;
    }

    track_audio_runtime_ctx_t target_ctx_value;
    const track_audio_runtime_ctx_t *const target_ctx =
        (audio_note_engine_adapter_current_ctx(
            target, &target_ctx_value) != 0U) ? &target_ctx_value : NULL;
    mod_destination_audio_models_t models;
    mod_matrix_audio_read_models(target, target_ctx, &models);
    return mod_destination_catalog_supported_audio(
        target, destination,
        mod_matrix_ui_family_from_ctx(target_ctx),
        mod_matrix_ui_type_from_ctx(target_ctx), target_ctx,
        &models);
}

static void mod_matrix_release_destination(mod_matrix_runtime_destination_t *dst)
{
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    (void)mod_destination_catalog_apply_prepared(&dst->prepared,
                                                 dst->base_value);

    dst->valid = 0U;
    dst->modulation_active = 0U;
    dst->destination = MOD_DESTINATION_NONE;
    dst->base_value = 0.0f;
    dst->sum = 0.0f;
    dst->sum_end = 0.0f;
    dst->min_value = 0.0f;
    dst->max_value = 127.0f;
    dst->ramp = (mod_destination_ramp_t){0};
    dst->prepared = (mod_destination_prepared_t){0};
}

static mod_matrix_runtime_destination_t *mod_matrix_find_runtime_destination(mod_matrix_runtime_track_t *rt,
                                                                            mod_destination_address_t destination)
{
    if (rt == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if ((rt->destinations[i].valid != 0U)
                && (rt->destinations[i].destination == destination))
        {
            return &rt->destinations[i];
        }
    }
    return NULL;
}

static mod_matrix_runtime_destination_t *mod_matrix_alloc_runtime_destination(mod_matrix_runtime_track_t *rt)
{
    if (rt == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0U; i < MOD_MATRIX_SLOT_COUNT; ++i)
    {
        if (rt->destinations[i].valid == 0U)
        {
            return &rt->destinations[i];
        }
    }
    return NULL;
}

static uint8_t mod_matrix_runtime_destination_prepare(uint8_t track,
                                                      mod_matrix_runtime_track_t *rt,
                                                      mod_destination_address_t destination,
                                                      float min_value,
                                                      float max_value,
                                                      mod_matrix_runtime_destination_t **out_dst)
{
    uint8_t target = 0U;
    param_id_t param = PARAM_COUNT;
    if ((track >= SEQ_TRACK_COUNT) || (rt == NULL) || (out_dst == NULL)
            || (mod_destination_address_resolve(destination, &target, &param) == 0U))
    {
        return 0U;
    }

    mod_matrix_runtime_destination_t *dst = mod_matrix_find_runtime_destination(rt, destination);
    track_audio_runtime_ctx_t target_ctx_value;
    const track_audio_runtime_ctx_t *const target_ctx =
        (audio_note_engine_adapter_current_ctx(
            target, &target_ctx_value) != 0U) ? &target_ctx_value : NULL;
    mod_destination_prepared_t prepared;
    mod_destination_audio_models_t models;
    mod_matrix_audio_read_models(target, target_ctx, &models);
    if ((target_ctx == NULL)
            || (mod_destination_catalog_prepare(target, param, target_ctx,
                                                &models,
                                                &prepared) == 0U))
        return 0U;
    if (dst == NULL)
    {
        dst = mod_matrix_alloc_runtime_destination(rt);
        if (dst == NULL)
        {
            return 0U;
        }

        /* Temporary initialization only; the CONTROL parameter command that
         * accompanies a newly routed destination seeds the authoritative
         * value before the next render. */
        float base_value = param_registry[param].default_value;
        dst->valid = 1U;
        dst->modulation_active = 0U;
        dst->destination = destination;
        dst->base_value = base_value;
        dst->sum = 0.0f;
        dst->sum_end = 0.0f;
        dst->min_value = min_value;
        dst->max_value = max_value;
        dst->ramp = (mod_destination_ramp_t){0};
    }

    dst->prepared = prepared;

    *out_dst = dst;
    return 1U;
}

static void mod_matrix_restore_destination_value(
    mod_matrix_runtime_destination_t *dst)
{
    if ((dst == NULL) || (dst->valid == 0U))
    {
        return;
    }

    (void)mod_destination_catalog_apply_prepared(&dst->prepared,
                                                 dst->base_value);
}

static void mod_matrix_rebuild_track_plan(uint8_t track,
                                          ui_track_family_t family,
                                          ui_track_type_t type,
                                          const track_audio_runtime_ctx_t *ctx,
                                          const mod_matrix_audio_state_t *state)
{
    typedef struct
    {
        uint8_t source;
        uint16_t destination;
        float depth;
        float min_value;
        float max_value;
    } mod_matrix_track_candidate_t;

    mod_matrix_track_candidate_t candidates[MOD_MATRIX_SLOT_COUNT];
    mod_matrix_track_plan_t *const plan = &g_mod_matrix_track_plan[track];
    mod_matrix_runtime_track_t *const rt = &g_mod_matrix_runtime[track];
    uint8_t candidate_count = 0U;

    memset(plan, 0, sizeof(*plan));

    for (uint8_t slot = 0U; slot < MOD_MATRIX_SLOT_COUNT; ++slot)
    {
        const track_mod_matrix_slot_t *const s = &state->slots[slot];
        if ((mod_matrix_slot_is_effective(track, s, family, type, ctx) != 0U)
                && (candidate_count < MOD_MATRIX_SLOT_COUNT))
        {
            candidates[candidate_count].source = s->source;
            candidates[candidate_count].destination = s->destination;
            candidates[candidate_count].depth = s->depth;
            uint8_t target = 0U;
            param_id_t destination = PARAM_COUNT;
            candidates[candidate_count].min_value = 0.0f;
            candidates[candidate_count].max_value = 127.0f;
            if (mod_destination_address_resolve(s->destination,
                                                &target, &destination) != 0U)
            {
                candidates[candidate_count].min_value = param_registry[destination].min;
                candidates[candidate_count].max_value = param_registry[destination].max;
                if ((destination == PARAM_AUDIO_FX_P1)
                        || (destination == PARAM_AUDIO_FX_B_P1))
                {
                    const uint8_t model = (destination == PARAM_AUDIO_FX_B_P1)
                        ? audio_fx_runtime_get_model(target, AUDIO_FX_SLOT_B)
                        : audio_fx_runtime_get_model(target, AUDIO_FX_SLOT_A);
                    if (model == AUDIO_FX_MODEL_DRIFT)
                        candidates[candidate_count].max_value =
                            FX_AUDIO_DRIFT_DELAY_MOD_MAX_CONTROL;
                }
            }
            ++candidate_count;
        }
    }

    for (uint8_t runtime_index = 0U; runtime_index < MOD_MATRIX_SLOT_COUNT; ++runtime_index)
    {
        mod_matrix_runtime_destination_t *const dst = &rt->destinations[runtime_index];
        uint8_t still_used = 0U;
        if ((dst->valid != 0U) && (dst->destination != MOD_DESTINATION_NONE))
        {
            for (uint8_t candidate = 0U; candidate < candidate_count; ++candidate)
            {
                if (candidates[candidate].destination == dst->destination)
                {
                    still_used = 1U;
                    break;
                }
            }
        }
        if ((dst->valid != 0U) && (still_used == 0U))
        {
            mod_matrix_release_destination(dst);
        }
    }

    for (uint8_t candidate = 0U; candidate < candidate_count; ++candidate)
    {
        uint8_t destination_index = plan->destination_count;
        for (uint8_t i = 0U; i < plan->destination_count; ++i)
        {
            const uint8_t runtime_index =
                plan->destinations[i].runtime_destination_index;
            if ((runtime_index < MOD_MATRIX_SLOT_COUNT)
                    && (rt->destinations[runtime_index].destination
                        == candidates[candidate].destination))
            {
                destination_index = i;
                break;
            }
        }

        if (destination_index == plan->destination_count)
        {
            mod_matrix_runtime_destination_t *dst = NULL;
            if ((plan->destination_count >= MOD_MATRIX_SLOT_COUNT)
                    || (mod_matrix_runtime_destination_prepare(
                            track,
                            rt,
                            candidates[candidate].destination,
                            candidates[candidate].min_value,
                            candidates[candidate].max_value,
                            &dst) == 0U))
            {
                continue;
            }

            const uint8_t runtime_index = (uint8_t)(dst - &rt->destinations[0]);
            if (runtime_index >= MOD_MATRIX_SLOT_COUNT)
            {
                continue;
            }

            destination_index = plan->destination_count;
            plan->destinations[destination_index].runtime_destination_index = runtime_index;
            ++plan->destination_count;
        }

        if (plan->route_count >= MOD_MATRIX_SLOT_COUNT)
        {
            continue;
        }

        mod_matrix_track_route_t *const route = &plan->routes[plan->route_count++];
        route->source = candidates[candidate].source;
        route->destination_index = destination_index;
        const uint8_t runtime_index =
            plan->destinations[destination_index].runtime_destination_index;
        route->scale = (candidates[candidate].depth / 127.0f)
            * (rt->destinations[runtime_index].max_value
               - rt->destinations[runtime_index].min_value);
        plan->destinations[destination_index].discontinuity_source_mask |=
            (uint16_t)(1U << candidates[candidate].source);
    }
}

