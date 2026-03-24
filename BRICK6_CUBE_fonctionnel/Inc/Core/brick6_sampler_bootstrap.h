/**
 * @file brick6_sampler_bootstrap.h
 * @brief Sampler bootstrap API.
 *
 * Rôle du module:
 * - Exposer le chargement sample pool et l'init des voix bootstrap.
 *
 * Frontière:
 * - Ne gère pas le runtime sampler complet.
 * - Ne décide pas de la policy UI de trigger.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void brick6_sampler_bootstrap_load_pool(void);
void brick6_sampler_bootstrap_init_voices(void);

#ifdef __cplusplus
}
#endif
