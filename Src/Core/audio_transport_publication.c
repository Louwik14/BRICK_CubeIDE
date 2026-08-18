#include "Core/audio_transport_publication.h"

#include <stddef.h>

#include "Storage/memory_layout.h"

D3_IPC static audio_transport_publication_t g_audio_transport_publication;

void audio_transport_publication_init(void)
{
    g_audio_transport_publication = (audio_transport_publication_t){0};
    g_audio_transport_publication.tempo_effective_bpm_milli = 120000U;
    g_audio_transport_publication.samples_per_step_q16 = 1U;
}

void audio_transport_publication_publish(uint8_t running,
                                         uint32_t tempo_effective_bpm_milli,
                                         uint32_t samples_per_step_q16)
{
    g_audio_transport_publication.running = (running != 0U) ? 1U : 0U;
    g_audio_transport_publication.tempo_effective_bpm_milli = tempo_effective_bpm_milli;
    g_audio_transport_publication.samples_per_step_q16 = samples_per_step_q16;
    g_audio_transport_publication.transport_epoch++;
}

void audio_transport_publication_set_running(uint8_t running)
{
    g_audio_transport_publication.running = (running != 0U) ? 1U : 0U;
    g_audio_transport_publication.transport_epoch++;
}

void audio_transport_publication_set_tempo(uint32_t tempo_effective_bpm_milli,
                                           uint32_t samples_per_step_q16)
{
    g_audio_transport_publication.tempo_effective_bpm_milli = tempo_effective_bpm_milli;
    g_audio_transport_publication.samples_per_step_q16 = samples_per_step_q16;
    g_audio_transport_publication.transport_epoch++;
}

void audio_transport_publication_get(audio_transport_publication_t *out)
{
    if (out != NULL)
    {
        *out = g_audio_transport_publication;
    }
}
