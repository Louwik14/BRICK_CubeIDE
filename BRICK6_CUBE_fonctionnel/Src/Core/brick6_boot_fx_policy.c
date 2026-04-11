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

#include "brick6_boot_fx_policy.h"

#include "fx_pool.h"
#include "mixer.h"

void brick6_boot_fx_policy_init(void)
{
    fx_pool_init();

    (void)fx_pool_activate_slot(0U, FX_EQ3);
    (void)fx_pool_activate_slot(1U, FX_SAT);
    (void)fx_pool_activate_slot(2U, FX_DAISY_COMP);

    /* Keep compressor slot available but do not insert it by default.
     * A permanent default insert on track 0 colors transients/dynamics even
     * with neutral user mix settings, which biases "flat/reference" listening. */
    mixer_set_track_insert_slot(0U, 0U, -1);
}
