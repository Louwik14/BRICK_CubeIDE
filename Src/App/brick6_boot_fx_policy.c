/**
 * @file brick6_boot_fx_policy.c
 * @brief Application de la policy FX au boot.
 *
 * Rôle du module:
 * - Activer les slots FX boot et le routage insert par défaut.
 *
 * Frontière:
 * - Ne pilote pas les changements FX runtime.
 * - Ne gère pas les paramètres FX utilisateurs.
 */

#include "App/brick6_boot_fx_policy.h"

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock.h"

void brick6_boot_fx_policy_init(void)
{
    /* Keep compressor slot available but do not insert it by default.
     * A permanent default insert on track 0 colors transients/dynamics even
     * with neutral user mix settings, which biases "flat/reference" listening. */
    uint64_t sample_time = 0U;
    if (live_clock_read_audio_sample(&sample_time))
        (void)control_audio_publish_param(
            0U, CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST,
            (uint32_t)(int32_t)-1, 0U, sample_time);
}
