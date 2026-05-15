/**
 * @file fx_pool.c
 * @brief Module applicatif fx_pool.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à fx_pool.
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

#include "fx_pool.h"

#include "fx_dj_eq3_cmsis.h"
#include "fx_saturation.h"
#include "fx_daisy_comp.h"
#include "mixer.h"
#include "memory_layout.h"
#include "stm32h7xx.h"

#define FX_POOL_SIZE 3u
#define FX_POOL_TRACK_SAT_COUNT MIXER_MAX_TRACKS

static fx_slot_t g_slots[FX_POOL_SIZE];

AUDIO_HOT static fx_dj_eq3_t g_eq;
static fx_saturation_t g_sat;
AUDIO_HOT static fx_saturation_t g_track_sat[FX_POOL_TRACK_SAT_COUNT];

/**
 * @brief Point d'entrée fx_pool_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_pool_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_pool_init(void)
{
    for (uint32_t i = 0u; i < FX_POOL_SIZE; ++i)
    {
        g_slots[i].active = 0u;
        g_slots[i].type = FX_NONE;
        g_slots[i].state = NULL;
    }

    for (uint32_t track = 0u; track < FX_POOL_TRACK_SAT_COUNT; ++track)
    {
        fx_saturation_init(&g_track_sat[track]);
    }
}

/**
 * @brief Point d'entrée fx_pool_activate_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_pool_activate_slot.
 *
 * @param index Paramètre d'entrée de l'API.
 * @param type Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
int fx_pool_activate_slot(uint32_t index, fx_type_t type)
{
    fx_slot_t* slot = NULL;

    if (index >= FX_POOL_SIZE)
        return 0;

    slot = &g_slots[index];
    fx_pool_deactivate_slot(index);

    switch (type)
    {
        case FX_EQ3:
            slot->state = &g_eq;
            break;

        case FX_SAT:
            slot->state = &g_sat;
            fx_saturation_init(&g_sat);
            for (uint32_t track = 0u; track < FX_POOL_TRACK_SAT_COUNT; ++track)
            {
                fx_saturation_init(&g_track_sat[track]);
            }
            break;

        case FX_GRANULAR:
            return 0;

        case FX_DAISY_COMP:
            slot->state = fx_daisy_comp_get_instance();
            break;

        default:
            return 0;
    }

    slot->type = (uint8_t)type;
    __DMB();
    slot->active = 1u;
    __DSB();
    return 1;
}

/**
 * @brief Point d'entrée fx_pool_deactivate_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_pool_deactivate_slot.
 *
 * @param index Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void fx_pool_deactivate_slot(uint32_t index)
{
    fx_slot_t* slot = NULL;

    if (index >= FX_POOL_SIZE)
        return;

    slot = &g_slots[index];

    slot->active = 0u;
    __DMB();

    slot->state = NULL;
    slot->type = FX_NONE;

    __DSB();
}

/**
 * @brief Point d'entrée fx_pool_get_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à fx_pool_get_slot.
 *
 * @param index Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
fx_slot_t* fx_pool_get_slot(uint32_t index)
{
    if (index >= FX_POOL_SIZE)
        return 0;

    return &g_slots[index];
}

void* fx_pool_get_sat_state_for_track(uint32_t track)
{
    if (track >= FX_POOL_TRACK_SAT_COUNT)
        return 0;

    return &g_track_sat[track];
}
