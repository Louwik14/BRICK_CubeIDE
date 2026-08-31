#include "IPC/control_audio_transport.h"

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_exec.h"

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

void control_audio_transport_publish_changes(void)
{
    const control_transport_last_t next = {
        .tempo = seq_runtime_get_tempo_bpm_milli(),
        .step_q16 = seq_runtime_get_samples_per_step_q16(),
        .running = seq_runtime_is_running()
    };
    const uint64_t sample = seq_runtime_exec_get_sample_timeline();
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
    if ((count == 0U) || (control_audio_publish_batch(commands,count) != 0U)) g_last=next;
}
