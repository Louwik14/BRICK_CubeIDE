#include "Audio/audio_transport_runtime.h"

#include "Audio/mixer.h"

static audio_transport_runtime_t g_audio_transport;

void audio_transport_runtime_init(void)
{
    g_audio_transport = (audio_transport_runtime_t){
        .tempo_effective_bpm_milli = 120000U,
        .samples_per_step_q16 = 1U
    };
}

const audio_transport_runtime_t *audio_transport_runtime_get(void)
{ return &g_audio_transport; }

uint8_t audio_transport_runtime_set_running(uint8_t running)
{ g_audio_transport.running = (running != 0U); return 1U; }

uint8_t audio_transport_runtime_set_tempo(uint32_t tempo_milli)
{
    if (tempo_milli == 0U) return 0U;
    if (g_audio_transport.tempo_effective_bpm_milli == tempo_milli) return 1U;
    g_audio_transport.tempo_effective_bpm_milli = tempo_milli;
    mixer_set_delay_transport_tempo_bpm_milli(tempo_milli);
    return 1U;
}

uint8_t audio_transport_runtime_set_step_q16(uint32_t samples_per_step_q16)
{ if (samples_per_step_q16 == 0U) return 0U; g_audio_transport.samples_per_step_q16 = samples_per_step_q16; return 1U; }
