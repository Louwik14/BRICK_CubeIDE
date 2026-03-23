/**
 * @file ui_param.c
 * @brief Module applicatif ui_param.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à ui_param.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "ui_param.h"

#include "param_registry.h"
#include "ui_core.h"

typedef struct
{
    ui_param_bank_t bank;
    uint8_t valid;
} ui_param_state_t;

static ui_param_state_t g_ui_param = {
    .bank = { .params = { PARAM_GRAN_DENSITY, PARAM_GRAN_PITCH, PARAM_GRAN_MIX, PARAM_GRAN_FREEZE } },
    .valid = 0U,
};

/**
 * @brief Point d'entrée ui_param_clamp.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_clamp.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param min Paramètre d'entrée de l'API.
 * @param max Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float ui_param_clamp(float v, float min, float max)
{
    if (v < min)
    {
        return min;
    }

    if (v > max)
    {
        return max;
    }

    return v;
}

static int8_t ui_param_signum(int16_t value)
{
    if (value > 0)
    {
        return 1;
    }

    if (value < 0)
    {
        return -1;
    }

    return 0;
}

static uint8_t ui_param_cfg_track_family_is_available(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    if (!ui_track_family_is_input(family))
    {
        return 1U;
    }

    if (family == ui_get_track_family(ui_get_active_track()))
    {
        return 1U;
    }

    return (uint8_t)((ui_count_tracks_with_family(family) == 0U) ? 1U : 0U);
}

static float ui_param_step_cfg_track(float current_value, int8_t direction)
{
    int16_t candidate = (int16_t)(current_value + 0.5f);

    if (direction == 0)
    {
        return current_value;
    }

    while (1)
    {
        candidate = (int16_t)(candidate + direction);

        if ((candidate < 0) || (candidate >= (int16_t)UI_TRACK_FAMILY_COUNT))
        {
            return current_value;
        }

        if (ui_param_cfg_track_family_is_available((ui_track_family_t)candidate) != 0U)
        {
            return (float)candidate;
        }
    }
}

static float ui_param_step_cfg_track_type(float current_value, int8_t direction)
{
    const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
    const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
    int16_t candidate = (int16_t)(current_value + 0.5f);

    if ((direction == 0) || (type_count == 0U))
    {
        return current_value;
    }

    candidate = (int16_t)(candidate + direction);
    if (candidate < 0)
    {
        candidate = 0;
    }
    if (candidate >= (int16_t)type_count)
    {
        candidate = (int16_t)type_count - 1;
    }

    return (float)candidate;
}


/**
 * @brief Point d'entrée ui_param_set_bank.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_set_bank.
 *
 * @param bank Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_param_set_bank(const ui_param_bank_t *bank)
{
    if (bank == 0)
    {
        g_ui_param.valid = 0U;
        return;
    }

    g_ui_param.bank = *bank;
    g_ui_param.valid = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        if (g_ui_param.bank.params[i] < PARAM_COUNT)
        {
            g_ui_param.valid = 1U;
            break;
        }
    }
}
/**
 * @brief Point d'entrée ui_param_handle_encoder.
 *
 * Rôle:
 * - Exécuter le traitement associé à ui_param_handle_encoder.
 *
 * @param encoder Paramètre d'entrée de l'API.
 * @param delta Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void ui_param_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((g_ui_param.valid == 0U) || (delta == 0) || (encoder >= 4U))
    {
        return;
    }

    const param_id_t param = g_ui_param.bank.params[encoder];
    if (param >= PARAM_COUNT)
    {
        return;
    }

    const param_desc_t *desc = &param_registry[param];
    float min_value = desc->min;
    float max_value = desc->max;

    if (param == PARAM_CFG_TRACK)
    {
        max_value = (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U);
    }
    else if (param == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
        const uint8_t type_count = ui_get_track_type_count_for_family(active_family);
        max_value = (type_count > 0U) ? (float)(type_count - 1U) : 0.0f;
    }

    float value = param_get(param);

    if (param == PARAM_CFG_TRACK)
    {
        value = ui_param_step_cfg_track(value, ui_param_signum(delta));
        param_set(param, value);
        return;
    }

    if (param == PARAM_CFG_TRACK_TYPE)
    {
        value = ui_param_step_cfg_track_type(value, ui_param_signum(delta));
        param_set(param, value);
        return;
    }

    value += (float)delta * desc->step;
    value = ui_param_clamp(value, min_value, max_value);

    param_set(param, value);
}
