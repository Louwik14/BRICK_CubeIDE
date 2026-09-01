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
#include "ControlRT/control_rt_publication.h"
#include "main.h"

void brick6_boot_fx_policy_init(void)
{
    /* Keep compressor slot available but do not insert it by default.
     * A permanent default insert on track 0 colors transients/dynamics even
     * with neutral user mix settings, which biases "flat/reference" listening. */
    if (control_rt_publish_param_now(
            0U, CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST,
            (uint32_t)(int32_t)-1, 0U) == 0U)
        Error_Handler();
}
