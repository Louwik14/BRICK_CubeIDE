static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped);
static float clamp_value(float v, float lo, float hi);


static void param_registry_sync_active_cfg_mirror_for_track(uint8_t track)
{
    if ((track >= SEQ_LANE_CAPACITY) || (track != ui_get_active_lane()))
    {
        return;
    }

    const track_family_t family = track_state_get_family(track);
    param_store_set_active(PARAM_CFG_TRACK, (float)family);
    param_store_set_active(PARAM_CFG_TRACK_TYPE,
                           (float)ui_track_catalog_type_index_for_family(family,
                                                                         track_state_get_type(track),
                                                                         track,
                                                                         track_state_get_configs()));
}

static uint8_t param_apply_cfg_track_value(param_id_t id, uint8_t track, float clamped)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    if (id == PARAM_CFG_TRACK)
    {
        const track_family_t requested_family =
            (track_family_t)((uint8_t)(clamp_value(clamped,
                                                      0.0f,
                                                      (float)((uint8_t)TRACK_FAMILY_COUNT - 1U)) + 0.5f));
        if (ui_set_track_family(track, requested_family) == false)
        {
            param_registry_sync_active_cfg_mirror_for_track(track);
            return 0U;
        }

        param_registry_sync_active_cfg_mirror_for_track(track);
        return 1U;
    }

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        const track_family_t family = track_state_get_family(track);
        const uint8_t requested_index =
            (uint8_t)(clamp_value(clamped, 0.0f, (float)((uint8_t)TRACK_TYPE_COUNT - 1U)) + 0.5f);
        const track_type_t requested_type =
            ui_track_catalog_type_from_family_index(family,
                                                    requested_index,
                                                    track,
                                                    track_state_get_configs());
        if (ui_set_track_type(track, requested_type) == false)
        {
            param_registry_sync_active_cfg_mirror_for_track(track);
            return 0U;
        }

        param_registry_sync_active_cfg_mirror_for_track(track);
        return 1U;
    }

    return 0U;
}

/**
 * @brief Point d'entrée clamp_value.
 *
 * Rôle:
 * - Exécuter le traitement associé à clamp_value.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param lo Paramètre d'entrée de l'API.
 * @param hi Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
