#include "Storage/project_load_quiesce.h"

#include "IPC/live_event.h"
#include "ControlRT/control_rt_publication.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime.h"
#include "Track/control_music_output.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "Storage/wav_convert.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/pattern_control_bank.h"
#include "Sampler/sample_page_lease_control.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/multi_sample_pool.h"
#include "midi.h"
#include "midi_host.h"
static volatile uint8_t g_project_load_panic_committed;
static volatile uint8_t g_project_load_retire_started;
static volatile uint8_t g_project_load_requested;
static volatile uint8_t g_project_load_request_pending;
static volatile uint8_t g_project_load_release_pending;
static volatile uint8_t g_project_load_ingress_open;

static uint8_t project_load_recorder_busy(void)
{
    audio_recorder_status_t status;
    if (audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                         &status) == 0U
        && audio_recorder_get_status_client(AUDIO_RECORDER_CLIENT_LOOPER,
                                            &status) == 0U)
        return 0U;
    if ((status.state != AUDIO_RECORDER_STATE_IDLE)
        && (status.state != AUDIO_RECORDER_STATE_FAILED)
        && (status.state != AUDIO_RECORDER_STATE_TAKE_READY))
        return 1U;
    return (uint8_t)((status.state == AUDIO_RECORDER_STATE_TAKE_READY)
        && (audio_recorder_looper_take_resource_retained() != 0U));
}

uint8_t project_load_allowed(void)
{
    return (uint8_t)((seq_runtime_is_running() == 0U)
        && (seq_runtime_is_start_pending() == 0U)
        && (pattern_control_bank_async_busy() == 0U)
        && (pattern_storage_is_pending() == 0U)
        && (project_load_recorder_busy() == 0U)
        && (sampler_ram_pool_load_async_busy() == 0U)
        && (wavetable_pool_load_async_busy() == 0U)
        && (multi_sample_load_has_pending() == 0U)
        && (multi_sample_pool_clear_is_active() == 0U)
        && (wav_convert_is_active() == 0U));
}

void project_load_quiesce_init(void)
{
    g_project_load_panic_committed = 0U;
    g_project_load_retire_started = 0U;
    g_project_load_requested = 0U;
    g_project_load_request_pending = 0U;
    g_project_load_release_pending = 0U;
    g_project_load_ingress_open = 1U;
}

void project_load_quiesce_request(void)
{
    if (g_project_load_requested != 0U
        || g_project_load_request_pending != 0U)
        return;
    g_project_load_request_pending = 1U;
}

void project_load_quiesce_control_process(void)
{
    if (g_project_load_release_pending != 0U)
    {
        g_project_load_release_pending = 0U;
        g_project_load_panic_committed = 0U;
        g_project_load_retire_started = 0U;
        g_project_load_requested = 0U;
        __DMB();
        g_project_load_ingress_open = 1U;
    }

    if (g_project_load_request_pending == 0U
        || g_project_load_requested != 0U)
        return;

    g_project_load_request_pending = 0U;
    g_project_load_ingress_open = 0U;
    __DMB();
    live_event_discard_pending();
    midi_rx_discard_pending();
    midi_host_rx_discard_pending();
    if (sd_preview_is_active() != 0U)
        sd_preview_stop();
    note_fx_pipeline_panic();
    seq_play_scheduler_clear();
    g_project_load_requested = 1U;
    g_project_load_panic_committed = control_music_output_panic_all(0U);
}

void project_load_quiesce_storage_retire(void)
{
    if (g_project_load_requested == 0U
        || g_project_load_panic_committed == 0U
        || g_project_load_retire_started != 0U)
        return;

    (void)sample_page_cache_cancel_reserved_domain(
        SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U);
    (void)sample_page_cache_cancel_reserved_domain(
        SAMPLE_AUDIO_DOMAIN_LOOPER, 0U);
    (void)sample_page_cache_cancel_reserved_domain(
        SAMPLE_AUDIO_DOMAIN_MULTI, 0U);
    sampler_ram_pool_retire_all();
    wavetable_pool_retire_all();
    multi_sample_pool_retire_all();
    __DMB();
    g_project_load_retire_started = 1U;
}

uint8_t project_load_quiesce_safe(void)
{
    if ((g_project_load_requested == 0U)
        || (g_project_load_panic_committed == 0U)
        || (g_project_load_retire_started == 0U)) return 0U;
    return (uint8_t)((g_project_load_panic_committed != 0U)
        && (g_project_load_retire_started != 0U)
        && (control_rt_publication_horizon_active() == 0U)
        && (sample_page_lease_control_all_released() != 0U)
        && (sample_cache_has_pending_sd_work() == 0U)
        && (sampler_ram_pool_retire_idle() != 0U)
        && (wavetable_pool_retire_idle() != 0U)
        && (multi_sample_pool_retire_idle() != 0U));
}

uint8_t project_load_quiesce_failed(void)
{
    if (g_project_load_requested == 0U) return 0U;
    return (uint8_t)((g_project_load_panic_committed == 0U)
        || (sampler_ram_pool_retire_failed() != 0U)
        || (wavetable_pool_retire_failed() != 0U)
        || (multi_sample_pool_retire_failed() != 0U));
}

void project_load_quiesce_end(void)
{
    g_project_load_release_pending = 1U;
}

uint8_t project_load_ingress_is_open(void)
{
    return g_project_load_ingress_open;
}

uint8_t project_replacement_is_active(void)
{
    return (g_project_load_ingress_open == 0U) ? 1U : 0U;
}
