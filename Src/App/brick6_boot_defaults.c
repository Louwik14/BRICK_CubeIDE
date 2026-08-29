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

#include "App/brick6_boot_defaults.h"

#include "param_registry.h"

void brick6_boot_apply_param_defaults(void)
{
    param_reset(PARAM_KBD_ROOT);
    param_reset(PARAM_KBD_SCALE);
    param_reset(PARAM_KBD_OMNICHORD);
    param_reset(PARAM_KBD_NOTE_ORDER);
    param_reset(PARAM_KBD_CHORD_OVERRIDE);
    param_reset(PARAM_MIX_SEND0_FX);
    param_reset(PARAM_MIX_SEND1_FX);
    param_reset(PARAM_MODFX_MODEL);
    param_reset(PARAM_MODFX_RATE);
    param_reset(PARAM_MODFX_DEPTH);
    param_reset(PARAM_MODFX_FEEDBACK);
    param_reset(PARAM_MODFX_OFFSET);
    param_reset(PARAM_BUS_COMP_THRESHOLD_DB);
    param_reset(PARAM_BUS_COMP_RATIO);
    param_reset(PARAM_BUS_COMP_ATTACK_INDEX);
    param_reset(PARAM_BUS_COMP_RELEASE_INDEX);
    param_reset(PARAM_BUS_COMP_MAKEUP_DB);
    param_reset(PARAM_BUS_COMP_AUTO_MAKEUP);
    param_reset(PARAM_BUS_COMP_DRYWET);
    param_reset(PARAM_BUS_COMP_HPF_HZ);
    param_reset(PARAM_EQ_LOW_DB);
    param_reset(PARAM_EQ_MID_DB);
    param_reset(PARAM_EQ_HIGH_DB);
    param_reset(PARAM_SAT_TONE);
    param_reset(PARAM_SAT_BIAS);
    param_reset(PARAM_SAT_DRIVE);
    param_reset(PARAM_SAT_MIX);
    param_reset(PARAM_COMP_MODEL);
    param_reset(PARAM_COMP_DETECT);
    param_reset(PARAM_COMP_KNEE_DB);
    param_reset(PARAM_COMP_DELUGE_SAT);
    param_reset(PARAM_MIX_REVERB_DELAYS);
    param_reset(PARAM_MIX_DELAY_TYPE);
    param_reset(PARAM_MIX_DELAY_TIME);
    param_reset(PARAM_MIX_DELAY_PINGPONG);
    param_reset(PARAM_MIX_DELAY_MODE);
    param_reset(PARAM_MIX_DELAY_TIME_R);
    param_reset(PARAM_MIX_DELAY_WIDTH);
    param_reset(PARAM_MIX_DELAY_FEEDBACK);
    param_reset(PARAM_MIX_DELAY_SPECTRAL_POSITION);
    param_reset(PARAM_MIX_DELAY_SPECTRAL_WIDTH);
    param_reset(PARAM_MIX_DELAY_FBW);
    param_reset(PARAM_MIX_DELAY_MOD);
    param_reset(PARAM_MIX_DELAY_MOD_RATE);
    param_reset(PARAM_MIX_DELAY_REV);
    param_reset(PARAM_MIX_DELAY_VOL);
    param_reset(PARAM_MIX_REVERB_WET);
    param_reset(PARAM_MIX_REVERB_ROOM_SIZE);
    param_reset(PARAM_MIX_REVERB_DAMPING);
    param_reset(PARAM_MIX_REVERB_WIDTH);
    param_reset(PARAM_MIX_REVERB_HPF);
    param_reset(PARAM_MIX_REVERB_LPF);
}
