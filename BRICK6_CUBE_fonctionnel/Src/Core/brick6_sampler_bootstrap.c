/**
 * @file brick6_sampler_bootstrap.c
 * @brief Bootstrap sampler (pool + voix de démarrage).
 *
 * Rôle du module:
 * - Isoler le chargement initial des samples et l'init voix bootstrap.
 *
 * Frontière:
 * - Ne contient pas le moteur sampler runtime.
 * - Ne remplace pas la logique métier future de preset/sessions.
 */

#include "brick6_sampler_bootstrap.h"

#include "Sampler/sample_pool.h"
#include "Sampler/voice_manager.h"
#include "Core/brick6_sd_config.h"

void brick6_sampler_bootstrap_load_pool(void)
{
    /* Toujours remettre le sample pool dans un état déterministe au boot. */
    sample_pool_init();

#if BRICK6_SD_ENABLE_BOOT_SAMPLE_LOAD
    sample_pool_load(0, "0:/Drum.wav");
    sample_pool_load(1, "0:/La ritournelle.wav");
#endif
}

void brick6_sampler_bootstrap_init_voices(void)
{
    /* Toujours remettre les voix dans un état neutre au boot. */
    voice_manager_init();

#if BRICK6_SD_ENABLE_BOOT_SAMPLE_LOAD
    /* Trigger bootstrap uniquement si le chargement sample boot est actif. */
    voice_manager_trigger(0, 0.30f, 0.30f);
    voice_manager_trigger(1, 0.30f, 0.30f);
#endif
}
