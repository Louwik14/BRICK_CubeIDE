#include "Core/audio_transport_publication.h"

#include <string.h>

#include "Seq/seq_runtime.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

D3_IPC static audio_transport_publication_t g_audio_transport_publication;
D3_IPC static volatile uint32_t g_audio_transport_publication_sequence;

void audio_transport_publication_init(void)
{
    memset(&g_audio_transport_publication, 0, sizeof(g_audio_transport_publication));
    g_audio_transport_publication_sequence = 0U;
    g_audio_transport_publication.tempo_effective_bpm_milli = 120000U;
    g_audio_transport_publication.samples_per_step_q16 = 1U;
    g_audio_transport_publication.transport_epoch = 1U;
}

void audio_transport_publication_refresh(void)
{
    audio_transport_publication_t next = {
        .running = seq_runtime_is_running(),
        .start_pending = seq_runtime_is_start_pending(),
        .tempo_effective_bpm_milli = seq_runtime_get_tempo_bpm_milli(),
        .samples_per_step_q16 = seq_runtime_get_samples_per_step_q16()
    };

    if ((g_audio_transport_publication.running != next.running)
            || (g_audio_transport_publication.start_pending != next.start_pending)
            || (g_audio_transport_publication.tempo_effective_bpm_milli
                != next.tempo_effective_bpm_milli)
            || (g_audio_transport_publication.samples_per_step_q16
                != next.samples_per_step_q16))
    {
        next.transport_epoch = g_audio_transport_publication.transport_epoch + 1U;
        if (next.transport_epoch == 0U)
            next.transport_epoch = 1U;
    }
    else
    {
        next.transport_epoch = g_audio_transport_publication.transport_epoch;
    }
    uint32_t sequence = g_audio_transport_publication_sequence;
    if ((sequence & 1U) != 0U)
        ++sequence;
    g_audio_transport_publication_sequence = sequence + 1U;
    __DMB();
    g_audio_transport_publication = next;
    __DMB();
    g_audio_transport_publication_sequence = sequence + 2U;
    if (g_audio_transport_publication_sequence == 0U)
        g_audio_transport_publication_sequence = 2U;
    __DMB();
}

uint8_t audio_transport_publication_read(
    audio_transport_publication_t *out_publication)
{
    if (out_publication == NULL)
        return 0U;
    for (uint8_t attempt = 0U; attempt < 4U; ++attempt)
    {
        const uint32_t before = g_audio_transport_publication_sequence;
        __DMB();
        if ((before == 0U) || ((before & 1U) != 0U))
            continue;
        *out_publication = g_audio_transport_publication;
        __DMB();
        if (before == g_audio_transport_publication_sequence)
            return 1U;
    }
    return 0U;
}
