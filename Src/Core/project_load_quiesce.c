#include "Core/project_load_quiesce.h"

#include "Audio/audio.h"
#include "Audio/audio_note_admission.h"
#include "Audio/control_audio_queue.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/live_parameter_audio_queue.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Storage/audio_recorder.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_preview.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t request_seq;
    volatile uint32_t audio_safe_seq;
    volatile uint32_t resume_seq;
    volatile uint32_t reserved[5];
} project_load_quiesce_mailbox_t;

_Static_assert(sizeof(project_load_quiesce_mailbox_t) == 32U,
               "Project Load quiesce mailbox ABI changed");

D3_IPC static project_load_quiesce_mailbox_t g_project_load_quiesce;

static uint32_t project_load_quiesce_next_seq(uint32_t sequence)
{
    ++sequence;
    return (sequence != 0U) ? sequence : 1U;
}

void project_load_quiesce_init(void)
{
    g_project_load_quiesce.request_seq = 0U;
    g_project_load_quiesce.audio_safe_seq = 0U;
    g_project_load_quiesce.resume_seq = 0U;
    for (uint8_t i = 0U; i < 5U; ++i)
        g_project_load_quiesce.reserved[i] = 0U;
    __DMB();
}

void project_load_quiesce_request(void)
{
    seq_runtime_stop();
    seq_output_guard_panic(1U);
    sd_preview_stop();
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_LOOPER);
    const uint32_t request = project_load_quiesce_next_seq(
        g_project_load_quiesce.request_seq);
    if (audio_get_init_state() != AUDIO_INIT_READY)
        g_project_load_quiesce.audio_safe_seq = request;
    __DMB();
    g_project_load_quiesce.request_seq = request;
    __DMB();
}

uint8_t project_load_quiesce_audio_service(void)
{
    const uint32_t request = g_project_load_quiesce.request_seq;
    __DMB();
    if ((request == 0U) || (g_project_load_quiesce.resume_seq == request))
    {
        return 0U;
    }
    if (g_project_load_quiesce.audio_safe_seq != request)
    {
        while (control_audio_queue_audio_pending_count() != 0U)
        {
            (void)control_audio_queue_audio_pop();
        }
        while (live_parameter_audio_queue_audio_pop())
        {
        }
        audio_note_admission_close_all();
        brick6_looper_runtime_arm_record_stop(0U);
        brick6_looper_runtime_on_transport_stop();
        __DMB();
        g_project_load_quiesce.audio_safe_seq = request;
        __DMB();
    }
    return 1U;
}

uint8_t project_load_quiesce_safe(void)
{
    __DMB();
    const uint32_t request = g_project_load_quiesce.request_seq;
    const uint32_t audio_safe = g_project_load_quiesce.audio_safe_seq;
    const uint32_t resume = g_project_load_quiesce.resume_seq;
    __DMB();
    return (uint8_t)((request != 0U)
        && (audio_safe == request)
        && (resume != request)
        && (sd_preview_is_active() == 0U)
        && (audio_recorder_is_active() == 0U));
}

void project_load_quiesce_end(void)
{
    const uint32_t request = g_project_load_quiesce.request_seq;
    __DMB();
    g_project_load_quiesce.resume_seq = request;
    __DMB();
}
