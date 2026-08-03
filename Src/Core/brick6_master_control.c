/**
 * @file brick6_master_control.c
 * @brief Traitement runtime du master depuis le pot mux.
 *
 * Rôle du module:
 * - Convertir la valeur pot en gain master et l'appliquer au mixer.
 *
 * Frontière:
 * - Ne fait pas de boot policy.
 * - Ne traite pas le reste des contrôles.
 */

#include "brick6_master_control.h"

#include <stdint.h>

#include "App/mux_pots.h"
#include "Board/board_surface.h"
#include "mixer.h"
#include "Param/param_macro.h"

#if defined(BRICK6_VARIANT_LOWCOST)
#define LOWCOST_FALLBACK_MASTER_GAIN 0.75f
#endif

void brick6_master_control_process(void)
{
    enum
    {
        POT_MACRO_BASE_INDEX = 0U,
        POT_MACRO_COUNT = 4U,
        POT_MASTER_INDEX = 4U,
        POT_RAW_MAX = 65535U,
        POT_MUTE_THRESHOLD = 1024U,
        POT_MASTER_STEPS = 512U
    };

    static uint8_t initialized = 0U;
#if !defined(BRICK6_VARIANT_LOWCOST)
    static uint16_t last_step = 0xFFFFU;
#endif
    static uint8_t macro_initialized[POT_MACRO_COUNT] = { 0U, 0U, 0U, 0U };
    static uint16_t macro_last_step[POT_MACRO_COUNT] = {
        0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU
    };
    static const uint8_t macro_pot_index_map[POT_MACRO_COUNT] = { 2U, 1U, 0U, 3U };

#if !defined(BRICK6_VARIANT_LOWCOST)
    for (uint8_t macro = 0U; macro < POT_MACRO_COUNT; ++macro)
    {
        const uint8_t pot_index = (uint8_t)(POT_MACRO_BASE_INDEX + macro_pot_index_map[macro]);
        if (mux_pots_is_valid(pot_index) == 0U)
        {
            continue;
        }

        const uint16_t raw = mux_pots_get(pot_index);
        uint16_t step = (uint16_t)(((uint32_t)raw * (uint32_t)(POT_MASTER_STEPS - 1U) + (POT_RAW_MAX / 2U))
                                   / POT_RAW_MAX);

        if (step >= POT_MASTER_STEPS)
        {
            step = (uint16_t)(POT_MASTER_STEPS - 1U);
        }

        if ((macro_initialized[macro] != 0U) && (macro_last_step[macro] == step))
        {
            continue;
        }

        (void)param_macro_set_amount(macro, (float)step / (float)(POT_MASTER_STEPS - 1U));
        macro_last_step[macro] = step;
        macro_initialized[macro] = 1U;
    }
#else
    (void)macro_initialized;
    (void)macro_last_step;
    (void)macro_pot_index_map;
#endif

#if defined(BRICK6_VARIANT_LOWCOST)
    uint16_t raw = 0U;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        if (initialized == 0U)
        {
            mixer_set_master(LOWCOST_FALLBACK_MASTER_GAIN);
            initialized = 1U;
        }
        return;
    }

    const float gain = (float)raw / 65535.0f;
    mixer_set_master(gain * gain);
    initialized = 1U;
    return;
#else
    if (mux_pots_is_valid(POT_MASTER_INDEX) == 0U)
    {
        return;
    }

    uint16_t raw = mux_pots_get(POT_MASTER_INDEX);

    if (raw <= POT_MUTE_THRESHOLD)
    {
        if ((initialized == 0U) || (last_step != 0U))
        {
            mixer_set_master(0.0f);
            last_step = 0U;
            initialized = 1U;
        }
        return;
    }

    raw = (uint16_t)(raw - POT_MUTE_THRESHOLD);

    const float norm = (float)raw / (float)(POT_RAW_MAX - POT_MUTE_THRESHOLD);
    uint16_t step = (uint16_t)(norm * (float)(POT_MASTER_STEPS - 1U) + 0.5f);

    if (step >= POT_MASTER_STEPS)
    {
        step = (uint16_t)(POT_MASTER_STEPS - 1U);
    }

    if ((initialized != 0U) &&
        (((step > last_step) ? (step - last_step) : (last_step - step)) < 1U))
    {
        return;
    }

    const float level = (float)step / (float)(POT_MASTER_STEPS - 1U);
    const float gain = level * level;

    mixer_set_master(gain);

    last_step = step;
    initialized = 1U;
#endif
}
