/**
 * @file brick6_master_control.c
 * @brief Traitement runtime du master depuis le pot mux.
 *
 * Rôle du module:
 * - Convertir la valeur pot en gain master et l'appliquer au mixer.
 *
 * Frontière:
 * - Capture et publie la valeur physique aux points imposés par le boot.
 * - N'initialise ni Hall/ADC ni le stream AUDIO.
 * - Ne traite pas le reste des contrôles.
 */

#include "App/brick6_master_control.h"

#include <stdint.h>

#include "App/mux_pots.h"
#include "Board/board_surface.h"
#include "Param/param_macro.h"
#include "Param/param_registry.h"

enum
{
    POT_MACRO_BASE_INDEX = 0U,
    POT_MACRO_COUNT = 4U,
    POT_MASTER_INDEX = 4U,
    POT_RAW_MAX = 65535U,
    POT_MUTE_THRESHOLD = 1024U,
    POT_MASTER_STEPS = 512U
};

static float g_boot_master_gain;
#if !defined(BRICK6_VARIANT_LOWCOST)
static uint16_t g_master_last_step = 0xFFFFU;
#else
static uint16_t g_master_last_raw;
#endif

#if !defined(BRICK6_VARIANT_LOWCOST)
static float brick6_master_control_premium_gain(uint16_t raw,
                                                uint16_t *out_step)
{
    uint16_t step = 0U;
    if (raw > POT_MUTE_THRESHOLD)
    {
        raw = (uint16_t)(raw - POT_MUTE_THRESHOLD);
        const float norm = (float)raw / (float)(POT_RAW_MAX - POT_MUTE_THRESHOLD);
        step = (uint16_t)(norm * (float)(POT_MASTER_STEPS - 1U) + 0.5f);
        if (step >= POT_MASTER_STEPS)
        {
            step = (uint16_t)(POT_MASTER_STEPS - 1U);
        }
    }

    *out_step = step;
    const float level = (float)step / (float)(POT_MASTER_STEPS - 1U);
    return level * level;
}
#endif

uint8_t brick6_master_control_boot_capture(void)
{
#if defined(BRICK6_VARIANT_LOWCOST)
    uint16_t raw;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        return 0U;
    }

    const float level = (float)raw / (float)POT_RAW_MAX;
    g_boot_master_gain = level * level;
    g_master_last_raw = raw;
#else
    if (mux_pots_is_valid(POT_MASTER_INDEX) == 0U)
    {
        return 0U;
    }

    g_boot_master_gain = brick6_master_control_premium_gain(
        mux_pots_get(POT_MASTER_INDEX), &g_master_last_step);
#endif
    return 1U;
}

void brick6_master_control_boot_publish(void)
{
    param_set(PARAM_MASTER_GAIN, g_boot_master_gain);
}

void brick6_master_control_process(void)
{
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
    uint16_t raw;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        return;
    }

    if (raw == g_master_last_raw)
    {
        return;
    }

    const float level = (float)raw / (float)POT_RAW_MAX;
    param_set(PARAM_MASTER_GAIN, level * level);
    g_master_last_raw = raw;
#else
    if (mux_pots_is_valid(POT_MASTER_INDEX) == 0U)
    {
        return;
    }

    uint16_t step;
    const float gain = brick6_master_control_premium_gain(
        mux_pots_get(POT_MASTER_INDEX), &step);
    if (step == g_master_last_step)
    {
        return;
    }

    param_set(PARAM_MASTER_GAIN, gain);
    g_master_last_step = step;
#endif
}
