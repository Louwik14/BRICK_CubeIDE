#include "IPC/control_audio_transport.h"

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_exec.h"
#include "Track/control_music_output.h"
#include "main.h"

typedef struct
{
    uint32_t tempo;
    uint32_t step_q16;
    uint8_t running;
} control_transport_last_t;

static control_transport_last_t g_last;

void control_audio_transport_init(void)
{
    g_last = (control_transport_last_t){ .tempo=120000U, .step_q16=1U };
}

static void control_audio_transport_publish_changes_at_internal(
    uint64_t sample, uint8_t asap)
{
    const control_transport_last_t next = {
        .tempo = seq_runtime_get_effective_tempo_bpm_milli(),
        .step_q16 = seq_runtime_get_samples_per_step_q16(),
        .running = seq_runtime_is_running()
    };
    control_audio_command_t commands[3];
    uint16_t count = 0U;
    if (next.running != g_last.running)
        commands[count++] = (control_audio_command_t){
            .effective_sample_time=sample,
            .opcode_kind=CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_TRANSPORT,
                next.running ? CONTROL_AUDIO_TRANSPORT_START : CONTROL_AUDIO_TRANSPORT_STOP) };
    if (next.tempo != g_last.tempo)
        commands[count++] = (control_audio_command_t){ .effective_sample_time=sample,
            .value=next.tempo, .id=CONTROL_AUDIO_PARAM_TRANSPORT_TEMPO,
            .opcode_kind=CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM,0U) };
    if (next.step_q16 != g_last.step_q16)
        commands[count++] = (control_audio_command_t){ .effective_sample_time=sample,
            .value=next.step_q16, .id=CONTROL_AUDIO_PARAM_TRANSPORT_STEP_Q16,
            .opcode_kind=CONTROL_AUDIO_COMMAND_TAG(CONTROL_AUDIO_COMMAND_PARAM,0U) };
    if (count == 0U)
    {
        g_last=next;
        return;
    }
    const uint8_t published = (asap != 0U)
        ? control_rt_publish_batch_now(commands, count)
        : control_rt_publish_batch_scheduled(commands, count);
    if (published == 0U)
    {
        /* g_last is a change-detection snapshot, never a retry queue. */
        Error_Handler();
        return;
    }
    g_last=next;
}

void control_audio_transport_publish_changes(void)
{
    control_audio_transport_publish_changes_at_internal(0U, 1U);
}

void control_audio_transport_publish_changes_at(uint64_t sample)
{
    control_audio_transport_publish_changes_at_internal(sample, 0U);
}
