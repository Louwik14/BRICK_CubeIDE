/**
 * @file brick6_sampler_bootstrap.c
 * @brief Bootstrap du sample pool.
 *
 * Rôle du module:
 * - Isoler l'initialisation deterministe du sample pool.
 *
 * Frontière:
 * - Ne contient pas le moteur sampler runtime.
 * - Ne remplace pas la logique métier future de preset/sessions.
 */

#include "brick6_sampler_bootstrap.h"

#include "Sampler/sample_pool.h"

void brick6_sampler_bootstrap_load_pool(void)
{
    /* Toujours remettre le sample pool dans un état déterministe au boot. */
    sample_pool_init();
}
