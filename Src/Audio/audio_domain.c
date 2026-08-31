#include "Audio/audio_domain.h"

#include "Audio/audio.h"
#include "Audio/audio_float.h"
#include "Audio/audio_wavetable_registry.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/live_parameter_audio_runtime.h"
#include "Audio/control_routing_audio.h"
#include "Audio/Engines/audio_engine_dispatch.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Board/board_audio.h"
#include "Platform/cpu_load.h"
#include "Sampler/sample_page_cache_audio.h"
#include "Audio/sample_page_lease_audio.h"
#include "IPC/control_audio_fifo_audio.h"

void audio_domain_init(const brick6_audio_boot_intent_t *boot_intent)
{
    if (boot_intent == 0)
    {
        return;
    }

    board_audio_codec_init();
    control_audio_fifo_audio_init();
    (void)brick6_audio_boot_apply_early(boot_intent);
    (void)brick6_audio_boot_apply_output_tracks(boot_intent);
    sample_page_cache_audio_init();
    sample_page_lease_audio_init();
    control_routing_audio_init();
    live_parameter_audio_runtime_init();
    (void)brick6_audio_boot_apply_drum(boot_intent);
    brick6_sampler_runtime_init();
    brick6_looper_runtime_init();
    (void)brick6_audio_boot_apply_engines(boot_intent);
    brick6_audio_boot_apply_binding_io();
    audio_set_float_callback(brick6_audio_runtime_dsp);
    cpu_load_reset_peak();
}

uint8_t audio_domain_start(void)
{
    return audio_start();
}

void audio_domain_background_poll(uint32_t byte_budget)
{
    brick6_sampler_runtime_service();
    brick6_sampler_runtime_service_physical_releases();
    brick6_looper_runtime_service(byte_budget);
}
