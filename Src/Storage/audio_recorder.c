#include "Storage/audio_recorder.h"

#include <string.h>

#include "ControlRT/control_rt_publication.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "Sampler/sample_page_cache.h"
#include "Storage/audio_recorder_storage.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/rec_latency_probe.h"
#include "Storage/rec_source.h"
#include "stm32h7xx.h"

volatile rec_latency_probe_t g_rec_latency_probe __attribute__((used));
uint32_t rec_latency_probe_now(void) { return TIM5->CNT; }
void rec_latency_probe_reset(void) {
    volatile uint32_t *p = (volatile uint32_t *)&g_rec_latency_probe;
    for(uint32_t i = 0U; i < sizeof(g_rec_latency_probe) / sizeof(uint32_t); ++i) p[i] = 0U;
}
void rec_latency_probe_service_reset(void) {
    if(g_rec_latency_probe.reset_request != 0U) rec_latency_probe_reset();
}
void rec_latency_probe_progress(void) {
    const uint32_t now = rec_latency_probe_now();
    if(g_rec_latency_probe.save_last_progress_t != 0U) {
        const uint32_t gap = now - g_rec_latency_probe.save_last_progress_t;
        if(gap > g_rec_latency_probe.save_max_progress_gap) g_rec_latency_probe.save_max_progress_gap = gap;
    }
    g_rec_latency_probe.save_last_progress_t = now;
    g_rec_latency_probe.save_progress_count++;
}

typedef struct
{
    audio_recorder_state_t state;
    audio_recorder_error_t error;
    audio_recorder_client_t client;
    uint32_t frame_limit;
    char temporary_path[AUDIO_RECORDER_PATH_MAX];
    char final_path[AUDIO_RECORDER_PATH_MAX];
} audio_recorder_runtime_t;

static audio_recorder_runtime_t g_audio_recorder;
static uint16_t g_audio_recorder_control_session;
static uint8_t g_build_stream_registered;
static sample_audio_key_t g_build_stream_key;
static uint32_t g_build_stream_readable_frames;

static void forget_build_stream(void)
{
    g_build_stream_registered = 0U;
    memset(&g_build_stream_key, 0, sizeof(g_build_stream_key));
    g_build_stream_readable_frames = 0U;
}

static void reset_session(void)
{
    memset(&g_audio_recorder, 0, sizeof(g_audio_recorder));
    g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
}

static uint8_t copy_path(char *dst, const char *src)
{
    if((dst == NULL) || (src == NULL) || (src[0] == '\0')) return 0U;
    for(uint32_t i = 0U; i < AUDIO_RECORDER_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0') return 1U;
    }
    dst[0] = '\0';
    return 0U;
}

static void register_build_stream(void)
{
    audio_recorder_storage_map_copy_t map;
    const uint32_t total_frames = g_audio_recorder_capture.head_cursor;
    const uint32_t readable_frames = (uint32_t)(
        audio_recorder_storage_committed_tail() / AUDIO_RECORDER_BYTES_PER_FRAME);
    if((g_audio_recorder.client != AUDIO_RECORDER_CLIENT_AUDIO_REC)
            || (rec_source_building_active() == 0U) || (total_frames == 0U)
            || (audio_recorder_storage_get_map_copy(&map) == 0U)
            || (map.reserved_file_bytes > UINT32_MAX)) return;
    const sample_audio_key_t key = rec_source_building_key();
    if(sample_page_cache_register_live_pcm24_stereo_sample_key(
            key, g_audio_recorder.temporary_path, total_frames,
            readable_frames, AUDIO_RECORDER_WAV_HEADER_BYTES,
            (uint32_t)map.reserved_file_bytes, map.extents,
            map.extent_count, map.media_epoch) == 0U) return;
    g_build_stream_registered = 1U;
    g_build_stream_key = key;
    g_build_stream_readable_frames = readable_frames;
    g_rec_latency_probe.preload_request_count++;
    g_rec_latency_probe.preload_pages_requested = SAMPLE_PAGE_MIN_READY_PAGES;
    if(g_rec_latency_probe.t_preload_first_request == 0U)
        g_rec_latency_probe.t_preload_first_request = rec_latency_probe_now();
    for(uint32_t page = 0U; page < SAMPLE_PAGE_MIN_READY_PAGES; ++page) {
        if(sample_page_cache_get_page_state_key(key, page) == SAMPLE_PAGE_READY)
            g_rec_latency_probe.preload_cache_hit_count++;
        else g_rec_latency_probe.preload_cache_miss_count++;
    }
    (void)sample_page_cache_reserve_start_pages_key(
        key, 0U, SAMPLE_PAGE_MIN_READY_PAGES);
    if(g_rec_latency_probe.t_preload_requested == 0U)
        g_rec_latency_probe.t_preload_requested = rec_latency_probe_now();
}

static void update_build_stream(void)
{
    if(g_build_stream_registered == 0U) return;
    const uint32_t readable_frames = (uint32_t)(
        audio_recorder_storage_committed_tail() / AUDIO_RECORDER_BYTES_PER_FRAME);
    if(readable_frames == g_build_stream_readable_frames) return;
    if(sample_page_cache_update_readable_frames_key(
            g_build_stream_key, readable_frames) != 0U)
        g_build_stream_readable_frames = readable_frames;
}

static uint8_t publish_start(audio_recorder_client_t client, uint64_t sample_time)
{
    if((g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder_control_session == 0U)) return 0U;
    if(control_rt_publish_record(CONTROL_AUDIO_RECORD_START,
            g_audio_recorder.frame_limit, g_audio_recorder_control_session,
            (uint8_t)client, sample_time) == 0U) return 0U;
    g_rec_latency_probe.t_rec_start = rec_latency_probe_now();
    g_audio_recorder.state = AUDIO_RECORDER_STATE_RECORDING;
    return 1U;
}

static uint8_t publish_stop(audio_recorder_client_t client, uint64_t sample_time)
{
    if(g_audio_recorder.client != client) return 0U;
    if(g_rec_latency_probe.t_stop_requested == 0U)
        g_rec_latency_probe.t_stop_requested = rec_latency_probe_now();
    g_rec_latency_probe.rec_duration_ticks =
        g_rec_latency_probe.t_stop_requested - g_rec_latency_probe.t_rec_start;
    return control_rt_publish_record(CONTROL_AUDIO_RECORD_STOP, 0U,
        g_audio_recorder_control_session, (uint8_t)client, sample_time);
}

void audio_recorder_init(void)
{
    reset_session();
    audio_recorder_storage_init();
    rec_source_init();
    g_audio_recorder_control_session = 0U;
    forget_build_stream();
}

audio_recorder_lifecycle_result_t audio_recorder_prepare_client_cooperative(
    audio_recorder_client_t client, const char *temporary_rec_path,
    const char *final_wav_path, uint32_t frame_limit)
{
    if(project_replacement_is_active() != 0U)
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    if((client != AUDIO_RECORDER_CLIENT_AUDIO_REC)
            || (temporary_rec_path == NULL) || (final_wav_path == NULL))
        return AUDIO_RECORDER_LIFECYCLE_ERROR;
    if((g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FAILED))
    {
        const audio_recorder_lifecycle_result_t discarded =
            audio_recorder_discard_client(g_audio_recorder.client);
        if(discarded != AUDIO_RECORDER_LIFECYCLE_OK) return discarded;
    }
    if(g_audio_recorder.state != AUDIO_RECORDER_STATE_IDLE)
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    char temporary_copy[AUDIO_RECORDER_PATH_MAX];
    char final_copy[AUDIO_RECORDER_PATH_MAX];
    if((copy_path(temporary_copy, temporary_rec_path) == 0U)
            || (copy_path(final_copy, final_wav_path) == 0U))
        return AUDIO_RECORDER_LIFECYCLE_ERROR;
    const audio_recorder_lifecycle_result_t prepared =
        audio_recorder_storage_prepare(temporary_copy, final_copy);
    if(prepared == AUDIO_RECORDER_LIFECYCLE_NOT_NOW) return prepared;
    (void)copy_path(g_audio_recorder.temporary_path, temporary_copy);
    (void)copy_path(g_audio_recorder.final_path, final_copy);
    g_audio_recorder.client = client;
    g_audio_recorder.frame_limit = (frame_limit != 0U) ? frame_limit
        : ((UINT32_MAX - AUDIO_RECORDER_WAV_HEADER_BYTES)
            / AUDIO_RECORDER_BYTES_PER_FRAME);
    if(prepared != AUDIO_RECORDER_LIFECYCLE_OK)
    {
        g_audio_recorder.error = audio_recorder_storage_error();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return AUDIO_RECORDER_LIFECYCLE_ERROR;
    }
    uint16_t session = (uint16_t)(g_audio_recorder_control_session + 1U);
    if(session == 0U) session = 1U;
    g_audio_recorder_control_session = session;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_PREPARED;
    g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
    return AUDIO_RECORDER_LIFECYCLE_OK;
}

uint8_t audio_recorder_prepare_client(audio_recorder_client_t client,
    const char *temporary_rec_path, const char *final_wav_path,
    uint32_t frame_limit)
{
    return (audio_recorder_prepare_client_cooperative(client,
        temporary_rec_path, final_wav_path, frame_limit)
        == AUDIO_RECORDER_LIFECYCLE_OK) ? 1U : 0U;
}

uint8_t audio_recorder_start_client_at(audio_recorder_client_t client,
                                       uint64_t sample_time)
{
    if(project_replacement_is_active() != 0U) return 0U;
    return publish_start(client, sample_time);
}

uint8_t audio_recorder_cancel_prepared_client(audio_recorder_client_t client)
{
    if((client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)) return 0U;
    return (audio_recorder_discard_client(client)
        == AUDIO_RECORDER_LIFECYCLE_OK) ? 1U : 0U;
}

audio_recorder_lifecycle_result_t audio_recorder_discard_client(
    audio_recorder_client_t client)
{
    if((client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder.client != client))
        return (g_audio_recorder.state == AUDIO_RECORDER_STATE_IDLE)
            ? AUDIO_RECORDER_LIFECYCLE_OK : AUDIO_RECORDER_LIFECYCLE_ERROR;
    if((g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING))
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    if((rec_source_building_active() == 0U)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY))
    {
        audio_recorder_storage_release();
        reset_session();
        return AUDIO_RECORDER_LIFECYCLE_OK;
    }
    const audio_recorder_lifecycle_result_t discarded =
        audio_recorder_storage_cancel();
    if(discarded == AUDIO_RECORDER_LIFECYCLE_NOT_NOW) return discarded;
    if((discarded == AUDIO_RECORDER_LIFECYCLE_ERROR)
            && (audio_recorder_storage_phase() != AUDIO_RECORDER_STORAGE_IDLE))
    {
        g_audio_recorder.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return discarded;
    }
    rec_source_abort_building();
    forget_build_stream();
    reset_session();
    return discarded;
}

uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client)
{
    uint64_t sample_time = 0U;
    if(control_rt_resolve_asap_sample(0U, &sample_time) == 0U) return 0U;
    return publish_stop(client, sample_time);
}

uint8_t audio_recorder_request_stop_client_at(audio_recorder_client_t client,
                                              uint64_t sample_time)
{
    return publish_stop(client, sample_time);
}

void audio_recorder_service(void)
{
    rec_latency_probe_service_reset();
    rec_source_service();
    audio_recorder_storage_service(g_audio_recorder_control_session,
        (uint8_t)((g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)));
    const audio_recorder_storage_phase_t phase = audio_recorder_storage_phase();
    if(phase == AUDIO_RECORDER_STORAGE_FAILED)
    {
        if((g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
                && (audio_recorder_request_stop_client(
                    g_audio_recorder.client) == 0U))
            Error_Handler();
        g_audio_recorder.error = audio_recorder_storage_error();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        rec_source_abort_building();
        forget_build_stream();
        (void)audio_recorder_storage_cancel();
    }
    else if((phase == AUDIO_RECORDER_STORAGE_DRAINING)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING))
    {
        g_audio_recorder.error = audio_recorder_storage_error();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
        register_build_stream();
    }
    else if(phase == AUDIO_RECORDER_STORAGE_FINALIZING)
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FINALIZING;
    else if(phase == AUDIO_RECORDER_STORAGE_TAKE_READY)
    {
        g_audio_recorder.state = AUDIO_RECORDER_STATE_TAKE_READY;
        if((g_build_stream_registered != 0U)
                && (rec_source_building_active() != 0U))
        {
            uint32_t epoch = 0U;
            audio_recorder_storage_map_copy_t map;
            const sample_audio_key_t key = rec_source_building_key();
            const uint32_t frames = (uint32_t)(
                audio_recorder_storage_committed_tail()
                    / AUDIO_RECORDER_BYTES_PER_FRAME);
            uint32_t ready_pages = (frames + SAMPLE_PAGE_FRAMES - 1U)
                / SAMPLE_PAGE_FRAMES;
            if(ready_pages > SAMPLE_PAGE_MIN_READY_PAGES)
                ready_pages = SAMPLE_PAGE_MIN_READY_PAGES;
            (void)sample_page_cache_update_stream_path_key(
                key, g_audio_recorder.final_path);
            (void)sample_page_cache_reserve_start_pages_key(key, 0U, ready_pages);
            uint8_t pages_ready = (ready_pages != 0U) ? 1U : 0U;
            for(uint32_t page = 0U; page < ready_pages; ++page)
                if(sample_page_cache_get_page_state_key(key, page)
                        != SAMPLE_PAGE_READY) pages_ready = 0U;
            if((pages_ready != 0U) && (g_rec_latency_probe.t_pages_ready == 0U))
                g_rec_latency_probe.t_pages_ready = rec_latency_probe_now();
            if((pages_ready != 0U) && (g_rec_latency_probe.t_preload_all_ready == 0U)) {
                g_rec_latency_probe.t_preload_all_ready = rec_latency_probe_now();
                g_rec_latency_probe.preload_pages_ready = ready_pages;
            }
            if((sample_page_cache_get_registration_epoch_key(key, &epoch) != 0U)
                    && (audio_recorder_storage_get_map_copy(&map) != 0U)
                    && (pages_ready != 0U)
                    && (rec_source_publish_building(frames, epoch, &map) != 0U))
            {
                g_rec_latency_probe.t_rec_source_published = rec_latency_probe_now();
                forget_build_stream();
            }
        }
    }
    update_build_stream();
}

uint8_t audio_recorder_get_status_client(audio_recorder_client_t client,
                                         audio_recorder_status_t *status)
{
    if((status == NULL) || (client != AUDIO_RECORDER_CLIENT_AUDIO_REC)
            || ((g_audio_recorder.client != AUDIO_RECORDER_CLIENT_NONE)
                && (g_audio_recorder.client != client))) return 0U;
    memset(status, 0, sizeof(*status));
    generic_recorder_status_t generic_status;
    audio_recorder_storage_get_status(&generic_status);
    status->state = g_audio_recorder.state;
    status->error = g_audio_recorder.error;
    status->frames_received = g_audio_recorder_capture.head_cursor;
    status->frames_assigned = (uint32_t)(
        generic_status.assigned_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_committed = (uint32_t)(
        generic_status.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_pending = status->frames_received - status->frames_committed;
    return 1U;
}

uint8_t audio_recorder_get_last_take_client(audio_recorder_client_t client,
                                            const char **path, uint32_t *frames)
{
    if((client != AUDIO_RECORDER_CLIENT_AUDIO_REC)
            || (g_audio_recorder.client != client) || (path == NULL)
            || (frames == NULL)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_TAKE_READY)) return 0U;
    *path = g_audio_recorder.final_path;
    *frames = (uint32_t)(audio_recorder_storage_committed_tail()
        / AUDIO_RECORDER_BYTES_PER_FRAME);
    return (*frames != 0U) ? 1U : 0U;
}

uint8_t audio_recorder_is_active(void)
{
    return (uint8_t)((g_audio_recorder.state == AUDIO_RECORDER_STATE_PREPARED)
        || (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
        || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
        || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING));
}

uint8_t audio_recorder_client_is_active(audio_recorder_client_t client)
{
    return (uint8_t)((g_audio_recorder.client == client)
        && (audio_recorder_is_active() != 0U));
}

uint8_t audio_recorder_client_is_recording(audio_recorder_client_t client)
{
    return (uint8_t)((g_audio_recorder.client == client)
        && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
        && (g_audio_recorder_capture.closed_session
            != g_audio_recorder_control_session));
}
