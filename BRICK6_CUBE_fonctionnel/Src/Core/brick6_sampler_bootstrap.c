#include "brick6_sampler_bootstrap.h"

#include "Sampler/sample_pool.h"
#include "Sampler/voice_manager.h"

void brick6_sampler_bootstrap_load_pool(void)
{
    sample_pool_init();

    sample_pool_load(0, "0:/Drum.wav");
    sample_pool_load(1, "0:/La ritournelle.wav");
}

void brick6_sampler_bootstrap_init_voices(void)
{
    voice_manager_init();

    /* Trigger immédiat conservé: bootstrap test/dev existant. */
    voice_manager_trigger(0, 0.30f, 0.30f);
    voice_manager_trigger(1, 0.30f, 0.30f);
}
