/**
 * @file brick6_boot_defaults.c
 * @brief Application des defaults paramètres au boot.
 *
 * Rôle du module:
 * - Regrouper l'installation des valeurs CONTROL par défaut au démarrage.
 *
 * Frontière:
 * - N'embarque aucune logique runtime.
 * - N'orchestre pas l'ordre global du boot.
 */

#include "App/brick6_boot_defaults.h"

#include "param_registry.h"

void brick6_boot_apply_param_defaults(void)
{
    const param_id_t globals[] = {
        PARAM_MIX_SEND0_FX, PARAM_MIX_SEND1_FX, PARAM_MODFX_MODEL,
        PARAM_MODFX_RATE, PARAM_MODFX_DEPTH, PARAM_MODFX_FEEDBACK,
        PARAM_MODFX_OFFSET, PARAM_BUS_COMP_THRESHOLD_DB, PARAM_BUS_COMP_RATIO,
        PARAM_BUS_COMP_ATTACK_INDEX, PARAM_BUS_COMP_RELEASE_INDEX,
        PARAM_BUS_COMP_MAKEUP_DB, PARAM_BUS_COMP_AUTO_MAKEUP,
        PARAM_BUS_COMP_DRYWET, PARAM_BUS_COMP_HPF_HZ, PARAM_EQ_LOW_DB,
        PARAM_EQ_MID_DB, PARAM_EQ_HIGH_DB, PARAM_SAT_TONE, PARAM_SAT_BIAS,
        PARAM_SAT_DRIVE, PARAM_SAT_MIX, PARAM_COMP_MODEL, PARAM_COMP_DETECT,
        PARAM_COMP_KNEE_DB, PARAM_COMP_DELUGE_SAT, PARAM_MIX_REVERB_DELAYS,
        PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_PINGPONG,
        PARAM_MIX_DELAY_MODE, PARAM_MIX_DELAY_WIDTH,
        PARAM_MIX_DELAY_FEEDBACK, PARAM_MIX_DELAY_SPECTRAL_POSITION,
        PARAM_MIX_DELAY_SPECTRAL_WIDTH, PARAM_MIX_DELAY_FBW,
        PARAM_MIX_DELAY_MOD, PARAM_MIX_DELAY_MOD_RATE, PARAM_MIX_DELAY_REV,
        PARAM_MIX_DELAY_VOL, PARAM_MIX_REVERB_WET,
        PARAM_MIX_REVERB_ROOM_SIZE, PARAM_MIX_REVERB_DAMPING,
        PARAM_MIX_REVERB_WIDTH, PARAM_MIX_REVERB_HPF, PARAM_MIX_REVERB_LPF
    };
    for (uint32_t i = 0U; i < (uint32_t)(sizeof(globals) / sizeof(globals[0])); ++i)
        (void)param_registry_reset_global(globals[i]);
}
