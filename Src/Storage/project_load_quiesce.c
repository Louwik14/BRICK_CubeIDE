#include "Storage/project_load_quiesce.h"

#include "IPC/control_audio_publication.h"
#include "IPC/live_event.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime.h"
#include "Track/control_music_output.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "midi.h"
#include "midi_host.h"
static uint32_t g_project_load_consumer_fence;
static uint8_t g_project_load_fence_valid;
static uint8_t g_project_load_requested;
static volatile uint8_t g_project_load_ingress_open;

static void project_load_close_old_sources(void)
{
    note_fx_pipeline_panic();
    seq_play_scheduler_clear();
    seq_runtime_stop();
}

void project_load_quiesce_init(void)
{
    g_project_load_consumer_fence = 0U;
    g_project_load_fence_valid = 0U;
    g_project_load_requested = 0U;
    g_project_load_ingress_open = 1U;
}

void project_load_quiesce_request(void)
{
    g_project_load_ingress_open = 0U;
    __DMB();
    live_event_discard_pending();
    midi_rx_discard_pending();
    midi_host_rx_discard_pending();
    sd_preview_stop();
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_LOOPER);
    g_project_load_requested = 1U;
    g_project_load_fence_valid = 0U;
    g_project_load_fence_valid = control_music_output_panic_all_fenced(
        &g_project_load_consumer_fence);
    if (g_project_load_fence_valid != 0U)
        project_load_close_old_sources();
}

uint8_t project_load_quiesce_safe(void)
{
    if ((g_project_load_requested != 0U)
            && (g_project_load_fence_valid == 0U))
    {
        g_project_load_fence_valid = control_music_output_panic_all_fenced(
            &g_project_load_consumer_fence);
        if (g_project_load_fence_valid != 0U)
            project_load_close_old_sources();
    }
    const uint8_t audio_safe = (uint8_t)(
        (g_project_load_fence_valid != 0U)
        && control_audio_consumer_fence_consumed(
            g_project_load_consumer_fence));
    return (uint8_t)(audio_safe
        && (sd_preview_is_active() == 0U)
        && (audio_recorder_is_active() == 0U));
}

void project_load_quiesce_end(void)
{
    g_project_load_fence_valid = 0U;
    g_project_load_requested = 0U;
    __DMB();
    g_project_load_ingress_open = 1U;
}

uint8_t project_load_ingress_is_open(void)
{
    return g_project_load_ingress_open;
}
