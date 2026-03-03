/**
 * @file control_router.c
 * @brief Routage des paramètres de contrôle vers param_store et mixer runtime.
 *
 * Rôle du module:
 * - Normaliser les commandes de contrôle entrantes.
 * - Écrire les valeurs dans param_store puis appliquer les mappings mixer.
 *
 * Architecture:
 * - Appelé par: UI/tasklets/contrôleurs.
 * - Appelle: param_store, mixer.
 *
 * Contraintes temps réel:
 * - IRQ: non requis (usage principal hors IRQ).
 * - Hard realtime: non.
 * - malloc: interdit.
 *
 * Notes:
 * - Le binding direct mixer est transitoire en attendant un binding 100% param_store.
 */

#include "control_router.h"

#include "mixer.h"
#include "param_store.h"
#include "audio_float.h"
#include "fx_bus_compressor.h"

/**
 * @brief Convertit une valeur float de contrôle en index de slot FX.
 *
 * @param v Valeur de contrôle.
 *
 * @return -1 si non assigné, sinon index tronqué.
 */
static int8_t control_float_to_slot(float v)
{
    if(v < 0.0f)
        return -1;
    return (int8_t)v;
}

/**
 * @brief Route un paramètre de contrôle vers le store et le mixer.
 *
 * @param id Identifiant du paramètre contrôlé.
 * @param v Valeur du paramètre.
 *
 * Rôle:
 * - Publier la valeur dans param_store (staging + tentative commit).
 * - Mettre à jour immédiatement l'état runtime du mixer concerné.
 *
 * Contexte d'appel:
 * - Tasklet/UI/main loop.
 */
void control_router_set_param(control_param_id_t id, float v)
{
    param_store_set_staging((param_id_t)id, v);
    (void)param_store_commit_if_block_advanced();

    switch(id)
    {
        case CTRL_PARAM_MIX_TRACK0_GAIN: mixer_set_track_gain(0U, v); break;
        case CTRL_PARAM_MIX_TRACK1_GAIN: mixer_set_track_gain(1U, v); break;
        case CTRL_PARAM_MIX_TRACK2_GAIN: mixer_set_track_gain(2U, v); break;
        case CTRL_PARAM_MIX_TRACK3_GAIN: mixer_set_track_gain(3U, v); break;

        case CTRL_PARAM_MIX_TRACK0_PAN: mixer_set_track_pan(0U, v); break;
        case CTRL_PARAM_MIX_TRACK1_PAN: mixer_set_track_pan(1U, v); break;
        case CTRL_PARAM_MIX_TRACK2_PAN: mixer_set_track_pan(2U, v); break;
        case CTRL_PARAM_MIX_TRACK3_PAN: mixer_set_track_pan(3U, v); break;

        case CTRL_PARAM_MIX_TRACK0_MUTE: mixer_set_track_mute(0U, (v >= 0.5f) ? 1U : 0U); break;
        case CTRL_PARAM_MIX_TRACK1_MUTE: mixer_set_track_mute(1U, (v >= 0.5f) ? 1U : 0U); break;
        case CTRL_PARAM_MIX_TRACK2_MUTE: mixer_set_track_mute(2U, (v >= 0.5f) ? 1U : 0U); break;
        case CTRL_PARAM_MIX_TRACK3_MUTE: mixer_set_track_mute(3U, (v >= 0.5f) ? 1U : 0U); break;

        case CTRL_PARAM_MIX_TRACK0_ROUTE: mixer_set_track_route(0U, (mixer_route_t)((uint32_t)v & 0x3U)); break;
        case CTRL_PARAM_MIX_TRACK1_ROUTE: mixer_set_track_route(1U, (mixer_route_t)((uint32_t)v & 0x3U)); break;
        case CTRL_PARAM_MIX_TRACK2_ROUTE: mixer_set_track_route(2U, (mixer_route_t)((uint32_t)v & 0x3U)); break;
        case CTRL_PARAM_MIX_TRACK3_ROUTE: mixer_set_track_route(3U, (mixer_route_t)((uint32_t)v & 0x3U)); break;

        case CTRL_PARAM_MIX_TRACK0_INSERT0: mixer_set_track_insert_slot(0U, 0U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK0_INSERT1: mixer_set_track_insert_slot(0U, 1U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK1_INSERT0: mixer_set_track_insert_slot(1U, 0U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK1_INSERT1: mixer_set_track_insert_slot(1U, 1U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK2_INSERT0: mixer_set_track_insert_slot(2U, 0U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK2_INSERT1: mixer_set_track_insert_slot(2U, 1U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK3_INSERT0: mixer_set_track_insert_slot(3U, 0U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_TRACK3_INSERT1: mixer_set_track_insert_slot(3U, 1U, control_float_to_slot(v)); break;

        case CTRL_PARAM_MIX_TRACK0_SEND0: mixer_set_track_send_level(0U, 0U, v); break;
        case CTRL_PARAM_MIX_TRACK0_SEND1: mixer_set_track_send_level(0U, 1U, v); break;
        case CTRL_PARAM_MIX_TRACK1_SEND0: mixer_set_track_send_level(1U, 0U, v); break;
        case CTRL_PARAM_MIX_TRACK1_SEND1: mixer_set_track_send_level(1U, 1U, v); break;
        case CTRL_PARAM_MIX_TRACK2_SEND0: mixer_set_track_send_level(2U, 0U, v); break;
        case CTRL_PARAM_MIX_TRACK2_SEND1: mixer_set_track_send_level(2U, 1U, v); break;
        case CTRL_PARAM_MIX_TRACK3_SEND0: mixer_set_track_send_level(3U, 0U, v); break;
        case CTRL_PARAM_MIX_TRACK3_SEND1: mixer_set_track_send_level(3U, 1U, v); break;

        case CTRL_PARAM_MIX_SEND0_FX: mixer_set_send_fx_slot(0U, control_float_to_slot(v)); break;
        case CTRL_PARAM_MIX_SEND1_FX: mixer_set_send_fx_slot(1U, control_float_to_slot(v)); break;

        case CTRL_PARAM_BUS_COMP_THRESHOLD_DB: audio_float_set_bus_comp_threshold_db(v); break;
        case CTRL_PARAM_BUS_COMP_RATIO: audio_float_set_bus_comp_ratio(v); break;
        case CTRL_PARAM_BUS_COMP_ATTACK_INDEX: audio_float_set_bus_comp_attack_index((uint8_t)v); break;
        case CTRL_PARAM_BUS_COMP_RELEASE_INDEX: audio_float_set_bus_comp_release_index((uint8_t)v); break;
        case CTRL_PARAM_BUS_COMP_MAKEUP_DB: fx_bus_compressor_set_makeup(v); break;
        case CTRL_PARAM_BUS_COMP_MIX: fx_bus_compressor_set_mix(v); break;
        case CTRL_PARAM_BUS_COMP_HPF_HZ: fx_bus_compressor_set_hpf(v); break;

        default:
            break;
    }
}
