/**
 * @file brick6_sampler_bootstrap.c
 * @brief Bootstrap sampler (pool + voix de démarrage).
 *
 * Rôle du module:
 * - Isoler l'initialisation deterministe du sample pool et l'init voix bootstrap.
 *
 * Frontière:
 * - Ne contient pas le moteur sampler runtime.
 * - Ne remplace pas la logique métier future de preset/sessions.
 */

#include "brick6_sampler_bootstrap.h"

#include "Sampler/sample_pool.h"
#include "Sampler/voice_manager.h"

void brick6_sampler_bootstrap_load_pool(void)
{
    /* Toujours remettre le sample pool dans un état déterministe au boot. */
    sample_pool_init();
}

void brick6_sampler_bootstrap_init_voices(void)
{
    /* Toujours remettre les voix dans un état neutre au boot. */
    voice_manager_init();
}
