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
#include "mixer.h"

void brick6_master_control_process(void)
{
    enum
    {
        POT_MASTER_INDEX = 4U,
        POT_RAW_MAX = 65535U,
        POT_MUTE_THRESHOLD = 1024U,
        POT_MASTER_STEPS = 512U
    };

    static uint8_t initialized = 0U;
    static uint16_t last_step = 0xFFFFU;

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
}
