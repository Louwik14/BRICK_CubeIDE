/**
 * @file brick6_master_control.c
 * @brief Traitement runtime du master depuis la surface BRICK.
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
};

static float g_boot_master_gain;
static uint16_t g_master_last_raw;

uint8_t brick6_master_control_boot_capture(void)
{
    uint16_t raw;
    if (board_surface_read_master_volume_raw(&raw) == 0U)
    {
        return 0U;
    }

    const float level = (float)raw / (float)POT_RAW_MAX;
    g_boot_master_gain = level * level;
    g_master_last_raw = raw;
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

    if (raw == g_master_last_raw)
    {
        return;
    }

    const float level = (float)raw / (float)POT_RAW_MAX;
    (void)param_registry_commit_global(PARAM_MASTER_GAIN, level * level);
    g_master_last_raw = raw;
}
