#include "Storage/project_load_quiesce.h"

#include "IPC/live_event.h"
#include "IPC/control_audio_publication.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime.h"
#include "Track/control_music_output.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "Sampler/sample_page_lease_control.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/multi_sample_pool.h"
#include "midi.h"
#include "midi_host.h"
static uint8_t g_project_load_panic_committed;
static uint8_t g_project_load_retire_started;
static uint8_t g_project_load_requested;
static volatile uint8_t g_project_load_ingress_open;

static void project_load_close_old_sources(void)
{
    note_fx_pipeline_panic();
    seq_play_scheduler_clear();
    seq_runtime_stop();
}

static void project_load_begin_physical_retire(void)
{
    project_load_close_old_sources();
    for (uint16_t i = 0U; i < SAMPLER_RAM_POOL_MAX_SLOTS; ++i)
        sampler_ram_pool_clear(i);
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
        wavetable_pool_clear(i);
    for (uint16_t i = 0U; i < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++i)
        (void)multi_sample_pool_clear_instrument(i);
    g_project_load_retire_started = 1U;
}

void project_load_quiesce_init(void)
{
    g_project_load_panic_committed = 0U;
    g_project_load_retire_started = 0U;
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
    g_project_load_panic_committed =
        (control_audio_publication_horizon_active() == 0U)
        ? control_music_output_panic_all(0U) : 0U;
    if (g_project_load_panic_committed != 0U)
        project_load_begin_physical_retire();
}

uint8_t project_load_quiesce_safe(void)
{
    if ((g_project_load_requested != 0U)
            && (g_project_load_panic_committed == 0U))
    {
        if (control_audio_publication_horizon_active() != 0U) return 0U;
        g_project_load_panic_committed = control_music_output_panic_all(0U);
        if (g_project_load_panic_committed != 0U)
            project_load_begin_physical_retire();
    }
    sampler_ram_pool_service_retire();
    wavetable_pool_service_retire();
    multi_sample_pool_service_retire();
    return (uint8_t)((g_project_load_panic_committed != 0U)
        && (g_project_load_retire_started != 0U)
        && (sd_preview_is_active() == 0U)
        && (audio_recorder_is_active() == 0U)
        && (sample_page_lease_control_all_released() != 0U)
        && (sampler_ram_pool_retire_idle() != 0U)
        && (wavetable_pool_retire_idle() != 0U)
        && (multi_sample_pool_retire_idle() != 0U));
}

void project_load_quiesce_end(void)
{
    g_project_load_panic_committed = 0U;
    g_project_load_retire_started = 0U;
    g_project_load_requested = 0U;
    __DMB();
    g_project_load_ingress_open = 1U;
}

uint8_t project_load_ingress_is_open(void)
{
    return g_project_load_ingress_open;
}
