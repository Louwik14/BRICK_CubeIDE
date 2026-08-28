#include "Core/project_load_quiesce.h"

#include "Audio/control_audio_command.h"
#include "Core/control_audio_publication.h"
#include "Core/live_clock.h"
#include "Core/live_event.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Seq/seq_output_guard.h"
#include "Seq/seq_runtime.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "midi.h"
#include "midi_host.h"
static uint32_t g_project_load_consumer_fence;
static uint8_t g_project_load_fence_valid;
static uint8_t g_project_load_requested;
static volatile uint8_t g_project_load_ingress_open;

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
    (void)note_fx_pipeline_request_panic();
    seq_runtime_stop();
    seq_output_guard_panic(1U);
    sd_preview_stop();
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
    (void)audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_LOOPER);
    uint64_t sample_time = 0U;
    g_project_load_requested = 1U;
    g_project_load_fence_valid = 0U;
    if (!live_clock_read_audio_sample(&sample_time))
        return;
    g_project_load_fence_valid = control_audio_publish_panic_fenced(
        CONTROL_AUDIO_PANIC_GLOBAL, 0U, sample_time,
        &g_project_load_consumer_fence);
}

uint8_t project_load_quiesce_safe(void)
{
    if ((g_project_load_requested != 0U)
            && (g_project_load_fence_valid == 0U))
    {
        uint64_t sample_time = 0U;
        if (live_clock_read_audio_sample(&sample_time))
            g_project_load_fence_valid = control_audio_publish_panic_fenced(
                CONTROL_AUDIO_PANIC_GLOBAL, 0U, sample_time,
                &g_project_load_consumer_fence);
    }
    live_clock_anchor_t anchor;
    const uint8_t audio_safe = (uint8_t)(
        (live_clock_read_anchor(&anchor) == false)
        || ((g_project_load_fence_valid != 0U)
            && control_audio_consumer_fence_consumed(
                g_project_load_consumer_fence)));
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
