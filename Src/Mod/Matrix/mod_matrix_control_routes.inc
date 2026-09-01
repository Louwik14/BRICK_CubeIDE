uint8_t mod_matrix_set_selected_slot(uint8_t track, float value)
{
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if (state == NULL)
    {
        return 0U;
    }

    state->mod_matrix_selected_slot = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SLOT_COUNT - 1U));
    return 1U;
}

uint8_t mod_matrix_get_selected_slot(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    if ((out_value == NULL) || (mod_matrix_track_selected_slot(track, &slot) == 0U))
    {
        return 0U;
    }

    *out_value = (float)slot;
    return 1U;
}

uint8_t mod_matrix_set_selected_slot_destination_index(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_destination_index(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_set_selected_slot_depth(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_depth(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_set_selected_slot_source(uint8_t track, float value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_set_slot_source(track, slot, value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_destination_index(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_destination_index(track, slot, out_value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_depth(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_depth(track, slot, out_value)
        : 0U;
}

uint8_t mod_matrix_get_selected_slot_source(uint8_t track, float *out_value)
{
    uint8_t slot = 0U;
    return (mod_matrix_track_selected_slot(track, &slot) != 0U)
        ? mod_matrix_get_slot_source(track, slot, out_value)
        : 0U;
}

enum { MOD_MATRIX_FIELD_SOURCE = 1U, MOD_MATRIX_FIELD_DEST = 2U,
       MOD_MATRIX_FIELD_DEPTH = 4U, MOD_MATRIX_FIELD_ENABLED = 8U };

static uint8_t mod_matrix_publish_slot_candidate(uint8_t track, uint8_t slot,
    const track_mod_matrix_slot_t *candidate, uint8_t fields,
    uint8_t include_destination_base)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT) || (slot >= MOD_MATRIX_SLOT_COUNT)
            || (candidate == NULL)) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK, .count = 0U
    };
#define ADD_MATRIX_FIELD(condition, parameter, field_value) do { \
    if (condition) bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){ \
        .parameter_id = (uint16_t)(parameter), .scope = LIVE_PARAMETER_EVENT_SCOPE_SLOT, \
        .track = owner, .slot = slot, \
        .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET \
            | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS), \
        .value = live_parameter_event_encode_float(field_value) }; \
} while (0)
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_SOURCE) != 0U,
                     CONTROL_AUDIO_MOD_ROUTE_SOURCE, (float)candidate->source);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEST) != 0U,
                     CONTROL_AUDIO_MOD_ROUTE_DESTINATION, (float)candidate->destination);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEPTH) != 0U,
                     CONTROL_AUDIO_MOD_ROUTE_DEPTH, candidate->depth);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_ENABLED) != 0U,
                     CONTROL_AUDIO_MOD_ROUTE_ENABLED, (float)(candidate->enabled != 0U));
#undef ADD_MATRIX_FIELD
    if ((include_destination_base != 0U)
            && (candidate->destination != MOD_DESTINATION_NONE))
    {
        uint8_t target = 0U;
        param_id_t destination = PARAM_COUNT;
        float base = 0.0f;
        param_registry_prepared_value_t prepared;
        if ((mod_destination_address_resolve(candidate->destination,
                    &target, &destination) == 0U)
                || (param_registry_get_track_value(destination, target, &base) == 0U)
                || (param_registry_prepare_value(destination, base, &prepared) == 0U))
            return 0U;
        bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
            .parameter_id = (uint16_t)destination,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = target,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(prepared.value)
        };
    }
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

static uint8_t mod_matrix_publish_operator_candidate(uint8_t track, uint16_t id,
                                           uint8_t index, float value)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT)) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK, .count = 1U,
        .item = {{ .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_SLOT, .track = owner,
            .slot = index,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(value) }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

uint8_t mod_matrix_set_slot_destination_index(uint8_t track, uint8_t slot, float value)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const uint16_t max_index = (uint16_t)(mod_destination_catalog_count(owner) - 1U);
    const uint16_t index = (uint16_t)mod_matrix_clampf(value, 0.0f, (float)max_index);
    const mod_destination_address_t new_destination =
        mod_destination_catalog_address_from_index(owner, index);
    const uint8_t enabled = ((new_destination != MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    if ((s->destination == new_destination) && (s->enabled == enabled))
        return 1U;
    track_mod_matrix_slot_t candidate = *s;
    candidate.destination = new_destination;
    candidate.enabled = enabled;
    if (mod_matrix_publish_slot_candidate(owner, slot, &candidate,
            MOD_MATRIX_FIELD_DEST | MOD_MATRIX_FIELD_ENABLED, 1U) == 0U)
        return 0U;
    s->destination = candidate.destination;
    s->enabled = candidate.enabled;
    return 1U;
}

uint8_t mod_matrix_set_slot_depth(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const float depth = mod_matrix_clampf(value, -127.0f, 127.0f);
    if (s->depth == depth)
        return 1U;
    track_mod_matrix_slot_t candidate = *s;
    candidate.depth = depth;
    if (!mod_matrix_publish_slot_candidate(track, slot, &candidate,
            MOD_MATRIX_FIELD_DEPTH, 0U)) return 0U;
    s->depth = candidate.depth;
    return 1U;
}

uint8_t mod_matrix_set_slot_source(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL)
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    const uint8_t enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    if ((s->source == source) && (s->enabled == enabled))
        return 1U;
    track_mod_matrix_slot_t candidate = *s;
    candidate.source = source;
    candidate.enabled = enabled;
    if (!mod_matrix_publish_slot_candidate(track, slot, &candidate,
            MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_ENABLED, 0U)) return 0U;
    s->source = candidate.source;
    s->enabled = candidate.enabled;
    return 1U;
}

uint8_t mod_matrix_set_slot_enabled(uint8_t track, uint8_t slot, float value)
{
    track_mod_matrix_slot_t *const s = mod_matrix_track_slot_mut(track, slot);
    if (s == NULL) return 0U;
    const uint8_t enabled = ((value >= 0.5f)
            && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (s->destination != MOD_DESTINATION_NONE)) ? 1U : 0U;
    if (s->enabled == enabled)
        return 1U;
    track_mod_matrix_slot_t candidate = *s;
    candidate.enabled = enabled;
    if (!mod_matrix_publish_slot_candidate(track, slot, &candidate,
            MOD_MATRIX_FIELD_ENABLED, 0U)) return 0U;
    s->enabled = candidate.enabled;
    return 1U;
}

uint8_t mod_matrix_set_slot_state(uint8_t track,
                                  uint8_t slot,
                                  uint8_t source,
                                  mod_destination_address_t destination,
                                  float depth,
                                  uint8_t enabled)
{
    brick_entity_id_t owner = track;
    if ((source >= MOD_MATRIX_SOURCE_COUNT)
            || (entity_topology_mod_owner(track, &owner) == 0U)
            || ((destination != MOD_DESTINATION_NONE)
                && (mod_destination_catalog_index_from_address(owner, destination) == UINT16_MAX)))
    {
        return 0U;
    }
    track_mod_matrix_slot_t *const state = mod_matrix_track_slot_mut(track, slot);
    if (state == NULL)
    {
        return 0U;
    }
    const float clamped_depth = mod_matrix_clampf(depth, -127.0f, 127.0f);
    const uint8_t effective_enabled = ((enabled != 0U)
            && (source != (uint8_t)MOD_MATRIX_SOURCE_NONE)
            && (destination != MOD_DESTINATION_NONE)) ? 1U : 0U;
    if ((state->source == source) && (state->destination == destination)
            && (state->depth == clamped_depth)
            && (state->enabled == effective_enabled))
        return 1U;
    const track_mod_matrix_slot_t candidate = {
        .source = source, .destination = destination,
        .depth = clamped_depth, .enabled = effective_enabled
    };
    if (!mod_matrix_publish_slot_candidate(owner, slot, &candidate,
        MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_DEST
        | MOD_MATRIX_FIELD_DEPTH | MOD_MATRIX_FIELD_ENABLED, 1U)) return 0U;
    *state = candidate;
    return 1U;
}

uint8_t mod_matrix_get_slot_destination_index(uint8_t track, uint8_t slot, float *out_value)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)mod_destination_catalog_index_from_address(owner, s->destination);
    return 1U;
}

uint8_t mod_matrix_get_slot_depth(uint8_t track, uint8_t slot, float *out_value)
{
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = s->depth;
    return 1U;
}

uint8_t mod_matrix_get_slot_source(uint8_t track, uint8_t slot, float *out_value)
{
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(track, slot);
    if ((s == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    *out_value = (float)s->source;
    return 1U;
}

uint8_t mod_matrix_set_multi_source(uint8_t track, uint8_t op, uint8_t input, float value)
{
    if ((op >= 2U) || (input >= 2U))
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if (state == NULL) return 0U;
    if (state->mod_multi_source[op][input] == source) return 1U;
    if (!mod_matrix_publish_operator_candidate(track, CONTROL_AUDIO_MOD_MULTI_SOURCE,
            (uint8_t)(op * 2U + input), (float)source)) return 0U;
    state->mod_multi_source[op][input] = source;
    return 1U;
}

uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value)
{
    if ((op >= 2U) || (input >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if (state == NULL) return 0U;
    *out_value = (float)state->mod_multi_source[op][input];
    return 1U;
}

uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if (state == NULL) return 0U;
    if (state->mod_slew_source[op] == source) return 1U;
    if (!mod_matrix_publish_operator_candidate(track, CONTROL_AUDIO_MOD_SLEW_SOURCE,
            op, (float)source)) return 0U;
    state->mod_slew_source[op] = source;
    return 1U;
}

uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if (state == NULL) return 0U;
    *out_value = (float)state->mod_slew_source[op];
    return 1U;
}

uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const float amount = mod_matrix_clampf(value, 0.0f, 1.0f);
    track_sound_state_t *const state = mod_matrix_control_state(track);
    if (state == NULL) return 0U;
    if (state->mod_slew_amount[op] == amount) return 1U;
    if (!mod_matrix_publish_operator_candidate(track, CONTROL_AUDIO_MOD_SLEW_AMOUNT,
            op, amount)) return 0U;
    state->mod_slew_amount[op] = amount;
    return 1U;
}

uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if (state == NULL) return 0U;
    *out_value = state->mod_slew_amount[op];
    return 1U;
}

