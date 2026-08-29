/**
 * @file brick6_boot_defaults.h
 * @brief Boot defaults policy API.
 *
 * Rôle du module:
 * - Exposer l'application des defaults paramètres au boot.
 *
 * Frontière:
 * - Ne définit pas les valeurs par défaut (portées par param_registry).
 * - N'orchestre pas le boot global (fait par brick6_app_init).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void brick6_boot_apply_param_defaults(void);

#ifdef __cplusplus
}
#endif
