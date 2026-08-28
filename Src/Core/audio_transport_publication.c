#include "Core/audio_transport_publication.h"

#include <string.h>

#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_exec.h"
#include "Core/control_audio_publication.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

D3_IPC static audio_transport_publication_t g_audio_transport_publication;
D3_IPC static volatile uint32_t g_audio_transport_publication_sequence;
static audio_transport_publication_t g_control_audio_transport_last;

void audio_transport_publication_init(void)
{
    memset(&g_audio_transport_publication, 0, sizeof(g_audio_transport_publication));
    g_audio_transport_publication_sequence = 0U;
    g_audio_transport_publication.tempo_effective_bpm_milli = 120000U;
    g_audio_transport_publication.samples_per_step_q16 = 1U;
    g_audio_transport_publication.transport_epoch = 1U;
    g_control_audio_transport_last = g_audio_transport_publication;
}

void audio_transport_publication_refresh(void)
{
    audio_transport_publication_t next = {
        .running = seq_runtime_is_running(),
        .start_pending = seq_runtime_is_start_pending(),
        .tempo_effective_bpm_milli = seq_runtime_get_tempo_bpm_milli(),
        .samples_per_step_q16 = seq_runtime_get_samples_per_step_q16()
    };

    const uint8_t running_changed = (uint8_t)(
        g_control_audio_transport_last.running != next.running);
    const uint8_t tempo_changed = (uint8_t)(
        g_control_audio_transport_last.tempo_effective_bpm_milli
            != next.tempo_effective_bpm_milli);
    const uint8_t step_changed = (uint8_t)(
        g_control_audio_transport_last.samples_per_step_q16
            != next.samples_per_step_q16);
    if ((running_changed != 0U)
            || (g_control_audio_transport_last.start_pending != next.start_pending)
            || (tempo_changed != 0U) || (step_changed != 0U))
    {
        next.transport_epoch = g_control_audio_transport_last.transport_epoch + 1U;
        if (next.transport_epoch == 0U)
            next.transport_epoch = 1U;
    }
    else
    {
        next.transport_epoch = g_control_audio_transport_last.transport_epoch;
    }
    const uint64_t sample_time = seq_runtime_exec_get_sample_timeline();
    control_audio_command_t commands[3];
    uint16_t count = 0U;
    if (running_changed != 0U)
        commands[count++] = (control_audio_command_t){
            .effective_sample_time = sample_time,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_TRANSPORT,
                next.running ? CONTROL_AUDIO_TRANSPORT_START
                             : CONTROL_AUDIO_TRANSPORT_STOP)
        };
    if (tempo_changed != 0U)
        commands[count++] = (control_audio_command_t){
            .effective_sample_time = sample_time,
            .value = next.tempo_effective_bpm_milli,
            .id = 0xFFDCU,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    if (step_changed != 0U)
        commands[count++] = (control_audio_command_t){
            .effective_sample_time = sample_time,
            .value = next.samples_per_step_q16,
            .id = 0xFFDDU,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    if ((count != 0U) && (control_audio_publish_batch(commands, count) == 0U))
        return;
    g_control_audio_transport_last = next;
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

uint8_t audio_transport_publication_audio_set_running(uint8_t running)
{
    g_audio_transport_publication.running = (running != 0U);
    g_audio_transport_publication.start_pending = 0U;
    g_audio_transport_publication_sequence = 2U;
    return 1U;
}

uint8_t audio_transport_publication_audio_set_tempo(uint32_t tempo_milli)
{
    if (tempo_milli == 0U) return 0U;
    g_audio_transport_publication.tempo_effective_bpm_milli = tempo_milli;
    g_audio_transport_publication_sequence = 2U;
    return 1U;
}

uint8_t audio_transport_publication_audio_set_step_q16(uint32_t samples_per_step_q16)
{
    if (samples_per_step_q16 == 0U) return 0U;
    g_audio_transport_publication.samples_per_step_q16 = samples_per_step_q16;
    g_audio_transport_publication_sequence = 2U;
    return 1U;
}
