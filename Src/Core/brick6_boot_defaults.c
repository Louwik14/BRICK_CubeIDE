/**
 * @file brick6_boot_defaults.c
 * @brief Application des defaults paramètres au boot.
 *
 * Rôle du module:
 * - Regrouper la séquence de param_reset(...) utilisée au démarrage.
 *
 * Frontière:
 * - N'embarque aucune logique runtime.
 * - N'orchestre pas l'ordre global du boot.
 */

#include "brick6_boot_defaults.h"

#include "param_registry.h"

void brick6_boot_apply_param_defaults(void)
{
    param_reset(PARAM_KBD_ROOT);
    param_reset(PARAM_KBD_SCALE);
    param_reset(PARAM_KBD_OMNICHORD);
    param_reset(PARAM_KBD_NOTE_ORDER);
    param_reset(PARAM_KBD_CHORD_OVERRIDE);
    param_reset(PARAM_MIX_REVERB_WET);
    param_reset(PARAM_MIX_REVERB_SIZE);
    param_reset(PARAM_MIX_REVERB_DECAY);
    param_reset(PARAM_MIX_REVERB_PRED);
    param_reset(PARAM_MIX_REVERB_HPF);
    param_reset(PARAM_MIX_REVERB_LPF);
    param_reset(PARAM_MIX_REVERB_DAMP);
    param_reset(PARAM_MIX_REVERB_SMEAR);
}
