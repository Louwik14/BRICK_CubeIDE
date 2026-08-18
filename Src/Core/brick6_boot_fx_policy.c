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
#include "Core/mixer_routing_publication.h"

void brick6_boot_fx_policy_init(void)
{
    fx_pool_init();

    (void)fx_pool_activate_slot(0U, FX_EQ3);
    (void)fx_pool_activate_slot(2U, FX_COMP_LAB);

    mixer_routing_control_init();

    /* Keep compressor slot available but do not insert it by default.
     * A permanent default insert on track 0 colors transients/dynamics even
     * with neutral user mix settings, which biases "flat/reference" listening. */
    (void)mixer_routing_control_set_insert_slot(0U, 0U, -1);
}
