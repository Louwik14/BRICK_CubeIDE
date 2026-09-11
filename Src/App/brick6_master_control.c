/**
 * @file brick6_master_control.c
 * @brief Traitement runtime du master depuis la surface LowCost.
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

#include "Board/board_surface.h"
#include "Param/param_registry.h"

enum
{
    POT_RAW_MAX = 65535U,
    POT_FILTER_SHIFT = 3U,
    POT_PUBLISH_HYSTERESIS = 64U
};

static float g_boot_master_gain;
static uint16_t g_master_filtered_raw;
static uint16_t g_master_last_published_raw;
static uint8_t g_master_filter_initialized;

static uint16_t brick6_master_filter_raw(uint16_t raw)
{
    if (g_master_filter_initialized == 0U)
    {
        g_master_filter_initialized = 1U;
        g_master_filtered_raw = raw;
        g_master_last_published_raw = raw;
        return raw;
    }

    if (raw > g_master_filtered_raw)
    {
        uint16_t step = (uint16_t)((raw - g_master_filtered_raw)
            >> POT_FILTER_SHIFT);
        if (step == 0U)
        {
            step = 1U;
        }
        g_master_filtered_raw = (uint16_t)(g_master_filtered_raw + step);
    }
    else if (raw < g_master_filtered_raw)
    {
        uint16_t step = (uint16_t)((g_master_filtered_raw - raw)
            >> POT_FILTER_SHIFT);
        if (step == 0U)
        {
            step = 1U;
        }
        g_master_filtered_raw = (uint16_t)(g_master_filtered_raw - step);
    }

    return g_master_filtered_raw;
}

static uint16_t brick6_master_raw_delta(uint16_t raw, uint16_t reference)
{
    return (raw >= reference)
        ? (uint16_t)(raw - reference)
        : (uint16_t)(reference - raw);
}


uint8_t brick6_master_control_boot_capture(void)
{
    uint16_t raw;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        return 0U;
    }

    const uint16_t filtered_raw = brick6_master_filter_raw(raw);
    const float level = (float)filtered_raw / (float)POT_RAW_MAX;
    g_boot_master_gain = level * level;
    return 1U;
}

void brick6_master_control_boot_publish(void)
{
    (void)param_registry_commit_global(PARAM_MASTER_GAIN, g_boot_master_gain);
}

void brick6_master_control_process(void)
{
    uint16_t raw;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        return;
    }

    const uint16_t filtered_raw = brick6_master_filter_raw(raw);
    if (brick6_master_raw_delta(filtered_raw, g_master_last_published_raw)
        < POT_PUBLISH_HYSTERESIS)
    {
        return;
    }

    const float level = (float)filtered_raw / (float)POT_RAW_MAX;
    if (param_registry_commit_global(PARAM_MASTER_GAIN, level * level) != 0U)
    {
        g_master_last_published_raw = filtered_raw;
    }
}
