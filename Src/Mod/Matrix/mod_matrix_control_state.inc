static float mod_matrix_clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static track_mod_matrix_slot_t *mod_matrix_track_slot_mut(uint8_t track, uint8_t slot)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return NULL;
    }
    track_sound_state_t *const state = track_sound_state_get(owner);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    track_mod_matrix_slot_t *const matrix_slot = &state->mod_matrix[slot];
    if (matrix_slot->destination < (mod_destination_address_t)PARAM_COUNT)
        matrix_slot->destination = mod_destination_address_make(
            owner, (param_id_t)matrix_slot->destination);
    return matrix_slot;
}

static const track_mod_matrix_slot_t *mod_matrix_track_slot_const(uint8_t track, uint8_t slot)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U)
    {
        return NULL;
    }
    const track_sound_state_t *const state = track_sound_state_get_const(owner);
    if ((state == NULL) || (slot >= MOD_MATRIX_SLOT_COUNT))
    {
        return NULL;
    }
    return &state->mod_matrix[slot];
}

static track_sound_state_t *mod_matrix_control_state(uint8_t track)
{
    brick_entity_id_t owner = track;
    return (entity_topology_mod_owner(track, &owner) != 0U)
        ? track_sound_state_get(owner) : NULL;
}

static const track_sound_state_t *mod_matrix_control_state_const(uint8_t track)
{
    brick_entity_id_t owner = track;
    return (entity_topology_mod_owner(track, &owner) != 0U)
        ? track_sound_state_get_const(owner) : NULL;
}

static void mod_matrix_recompute_global_route_flag(void)
{
    g_mod_matrix_any_route = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (g_mod_matrix_route_cache[track].any_route != 0U)
        {
            g_mod_matrix_any_route = 1U;
            return;
        }
    }
}

static uint8_t mod_matrix_track_selected_slot(uint8_t track, uint8_t *out_slot)
{
    const track_sound_state_t *const state = mod_matrix_control_state_const(track);
    if ((state == NULL) || (out_slot == NULL))
    {
        return 0U;
    }

    *out_slot = (state->mod_matrix_selected_slot < MOD_MATRIX_SLOT_COUNT) ? state->mod_matrix_selected_slot : 0U;
    return 1U;
}

