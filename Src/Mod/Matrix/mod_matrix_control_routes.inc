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

static uint8_t mod_matrix_publish_slot_fields(uint8_t track, uint8_t slot,
                                              uint8_t fields)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT) || (slot >= MOD_MATRIX_SLOT_COUNT)) return 0U;
    const track_mod_matrix_slot_t *const s = mod_matrix_track_slot_const(owner, slot);
    if (s == NULL) return 0U;
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
                     PARAM_MOD_MATRIX_SOURCE, (float)s->source);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEST) != 0U,
                     PARAM_MOD_MATRIX_DEST, (float)s->destination);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_DEPTH) != 0U,
                     PARAM_MOD_MATRIX_DEPTH, s->depth);
    ADD_MATRIX_FIELD((fields & MOD_MATRIX_FIELD_ENABLED) != 0U,
                     PARAM_MOD_MATRIX_SLOT, (float)(s->enabled != 0U));
#undef ADD_MATRIX_FIELD
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

static uint8_t mod_matrix_publish_operator(uint8_t track, param_id_t id,
                                           float value)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= SEQ_TRACK_COUNT)) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK, .count = 1U,
        .item = {{ .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK, .track = owner,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
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
    s->destination = new_destination;
    s->enabled = enabled;
    uint8_t target = 0U;
    param_id_t destination = PARAM_COUNT;
    float base = 0.0f;
    if ((mod_destination_address_resolve(s->destination, &target, &destination) != 0U)
            && (param_registry_get_track_value(destination, target, &base) != 0U))
        (void)param_registry_project_track_base_audio(destination, target, base);
    return mod_matrix_publish_slot_fields(owner, slot,
        MOD_MATRIX_FIELD_DEST | MOD_MATRIX_FIELD_ENABLED);
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
    s->depth = depth;
    return mod_matrix_publish_slot_fields(track, slot, MOD_MATRIX_FIELD_DEPTH);
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
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    if ((s->source == source) && (s->enabled == enabled))
        return 1U;
    s->source = source;
    s->enabled = ((s->destination != (uint16_t)MOD_DESTINATION_NONE)
                  && (s->source != (uint8_t)MOD_MATRIX_SOURCE_NONE)) ? 1U : 0U;
    return mod_matrix_publish_slot_fields(track, slot,
        MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_ENABLED);
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
    s->enabled = enabled;
    return mod_matrix_publish_slot_fields(track, slot, MOD_MATRIX_FIELD_ENABLED);
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
    state->source = source;
    state->destination = destination;
    state->depth = clamped_depth;
    state->enabled = effective_enabled;
    return mod_matrix_publish_slot_fields(owner, slot,
        MOD_MATRIX_FIELD_SOURCE | MOD_MATRIX_FIELD_DEST
        | MOD_MATRIX_FIELD_DEPTH | MOD_MATRIX_FIELD_ENABLED);
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
    static const param_id_t ids[2][2] = {
        { PARAM_MOD_MULTI_1_A, PARAM_MOD_MULTI_1_B },
        { PARAM_MOD_MULTI_2_A, PARAM_MOD_MULTI_2_B }
    };
    float current = 0.0f;
    if ((param_registry_control_value_get(track, ids[op][input], &current) != 0U)
            && ((uint8_t)(current + 0.5f) == source)) return 1U;
    param_registry_control_value_set(track, ids[op][input], (float)source);
    return mod_matrix_publish_operator(track, ids[op][input], (float)source);
}

uint8_t mod_matrix_get_multi_source(uint8_t track, uint8_t op, uint8_t input, float *out_value)
{
    if ((op >= 2U) || (input >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    static const param_id_t ids[2][2] = {
        { PARAM_MOD_MULTI_1_A, PARAM_MOD_MULTI_1_B },
        { PARAM_MOD_MULTI_2_A, PARAM_MOD_MULTI_2_B } };
    return param_registry_control_value_get(track, ids[op][input], out_value);
}

uint8_t mod_matrix_set_slew_source(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const uint8_t source = (uint8_t)mod_matrix_clampf(value, 0.0f, (float)(MOD_MATRIX_SOURCE_COUNT - 1U));
    const param_id_t id = (op == 0U) ? PARAM_MOD_SLEW_1_SOURCE
                                      : PARAM_MOD_SLEW_2_SOURCE;
    float current = 0.0f;
    if ((param_registry_control_value_get(track, id, &current) != 0U)
            && ((uint8_t)(current + 0.5f) == source)) return 1U;
    param_registry_control_value_set(track, id, (float)source);
    return mod_matrix_publish_operator(track, id, (float)source);
}

uint8_t mod_matrix_get_slew_source(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    return param_registry_control_value_get(track,
        (op == 0U) ? PARAM_MOD_SLEW_1_SOURCE : PARAM_MOD_SLEW_2_SOURCE,
        out_value);
}

uint8_t mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float value)
{
    if (op >= 2U)
    {
        return 0U;
    }

    const float amount = mod_matrix_clampf(value, 0.0f, 1.0f);
    const param_id_t id = (op == 0U) ? PARAM_MOD_SLEW_1_AMOUNT
                                      : PARAM_MOD_SLEW_2_AMOUNT;
    float current = 0.0f;
    if ((param_registry_control_value_get(track, id, &current) != 0U)
            && (current == amount)) return 1U;
    param_registry_control_value_set(track, id, amount);
    return mod_matrix_publish_operator(track, id, amount);
}

uint8_t mod_matrix_get_slew_amount(uint8_t track, uint8_t op, float *out_value)
{
    if ((op >= 2U) || (out_value == NULL))
    {
        return 0U;
    }

    return param_registry_control_value_get(track,
        (op == 0U) ? PARAM_MOD_SLEW_1_AMOUNT : PARAM_MOD_SLEW_2_AMOUNT,
        out_value);
}

