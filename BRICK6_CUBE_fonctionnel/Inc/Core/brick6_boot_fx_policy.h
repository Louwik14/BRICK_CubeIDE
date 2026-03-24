/**
 * @file brick6_boot_fx_policy.h
 * @brief Boot FX policy API.
 *
 * Rôle du module:
 * - Exposer l'application de la policy FX au démarrage.
 *
 * Frontière:
 * - Ne traite pas le routing runtime dynamique.
 * - Ne gère pas les paramètres FX UI.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void brick6_boot_fx_policy_init(void);

#ifdef __cplusplus
}
#endif
