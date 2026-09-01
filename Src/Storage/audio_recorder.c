#include "Storage/audio_recorder.h"

#include <string.h>

#include "Sampler/sample_audio_key.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_lease_control.h"
#include "Storage/audio_recorder_storage.h"
#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "IPC/live_clock_control.h"
#include "Storage/looper_storage.h"
#include "Storage/wav_loader.h"
#include "Storage/waveform_cache.h"
#include "Storage/project_load_quiesce.h"
#include "Track/control_routing.h"
#include "Track/track_state.h"
#define AUDIO_RECORDER_LOOPER_STEPS_PER_BAR 16U

typedef struct
{
    audio_recorder_state_t state;
    audio_recorder_error_t error;
    audio_recorder_client_t client;
    uint32_t frame_limit;
    char temporary_path[AUDIO_RECORDER_PATH_MAX];
    char final_path[AUDIO_RECORDER_PATH_MAX];
} audio_recorder_runtime_t;

/* Product/session state only; physical writer state belongs to Storage. */
static audio_recorder_runtime_t g_audio_recorder;

static uint16_t g_audio_recorder_control_session;

typedef struct
{
    uint64_t actual_start_sample;
    uint64_t target_stop_sample;
    uint32_t expected_frames;
    uint8_t track;
    uint8_t start_armed;
    uint8_t stop_armed;
    uint8_t recording;
    uint8_t overdub;
} audio_recorder_looper_control_t;

static audio_recorder_looper_control_t g_audio_recorder_looper_control;
static audio_recorder_looper_config_t
    g_audio_recorder_looper_config[BRICK_ENTITY_CAPACITY];
static uint8_t g_audio_recorder_looper_take_track = 0xFFU;
static uint8_t g_audio_recorder_looper_take_notified;
static uint8_t g_audio_recorder_looper_admission_open;
static uint8_t g_audio_recorder_looper_stream_registered;
static sample_audio_key_t g_audio_recorder_looper_stream_key;
static uint32_t g_audio_recorder_looper_stream_readable_frames;

static void audio_recorder_reset_looper_control(void)
{
    memset(&g_audio_recorder_looper_control, 0,
           sizeof(g_audio_recorder_looper_control));
    g_audio_recorder_looper_control.track = 0xFFU;
}

uint8_t audio_recorder_control_get_looper_config(
    uint8_t track, audio_recorder_looper_config_t *out_config)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (out_config == NULL)) return 0U;
    *out_config = g_audio_recorder_looper_config[track];
    return 1U;
}

uint8_t audio_recorder_control_set_looper_config(
    uint8_t track, const audio_recorder_looper_config_t *config)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (config == NULL)
            || (config->arm_mode > 2U) || (config->length_mode > 5U)
            || (config->play_auto > 1U)) return 0U;
    uint64_t sample = 0U;
    if (config->play_auto != g_audio_recorder_looper_config[track].play_auto)
    {
        if ((control_rt_now_sample(&sample) == 0U)
                || (control_rt_publish_param(track,
                    CONTROL_AUDIO_LOOPER_PLAY_AUTO, config->play_auto,
                    0U, sample) == 0U))
        {
            Error_Handler();
            return 0U;
        }
    }
    g_audio_recorder_looper_config[track] = *config;
    return 1U;
}

static void audio_recorder_retire_looper_stream(void)
{
    if (g_audio_recorder_looper_stream_registered == 0U) return;
    sample_page_cache_clear_key(g_audio_recorder_looper_stream_key);
    g_audio_recorder_looper_stream_registered = 0U;
    memset(&g_audio_recorder_looper_stream_key, 0,
           sizeof(g_audio_recorder_looper_stream_key));
    g_audio_recorder_looper_stream_readable_frames = 0U;
}

static void audio_recorder_register_looper_stream(void)
{
    audio_recorder_storage_map_copy_t map;
    const uint32_t total_frames = g_audio_recorder_capture.head_cursor;
    const uint32_t readable_frames = (uint32_t)(
        audio_recorder_storage_committed_tail()
            / AUDIO_RECORDER_BYTES_PER_FRAME);
    const uint8_t track = g_audio_recorder_looper_take_track;
    if (g_audio_recorder.client != AUDIO_RECORDER_CLIENT_LOOPER) return;

    audio_recorder_retire_looper_stream();
    if ((g_audio_recorder_looper_admission_open == 0U)
            || (track >= BRICK_ENTITY_CAPACITY)
            || (total_frames == 0U)
            || (audio_recorder_storage_get_map_copy(&map) == 0U)
            || (map.reserved_file_bytes > UINT32_MAX))
        return;

    const sample_audio_key_t key = sample_audio_key_looper(track);
    if (sample_page_cache_register_live_pcm24_stereo_sample_key(
            key, g_audio_recorder.temporary_path, total_frames,
            readable_frames, AUDIO_RECORDER_WAV_HEADER_BYTES,
            (uint32_t)map.reserved_file_bytes, map.extents,
            map.extent_count, map.media_epoch) == 0U)
        return;
    g_audio_recorder_looper_stream_registered = 1U;
    g_audio_recorder_looper_stream_key = key;
    g_audio_recorder_looper_stream_readable_frames = readable_frames;
}

static void audio_recorder_enter_draining(void)
{
    if (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING) return;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
    audio_recorder_register_looper_stream();
}

static void audio_recorder_update_looper_stream_readable(void)
{
    if (g_audio_recorder_looper_stream_registered == 0U) return;
    const uint32_t readable_frames = (uint32_t)(
        audio_recorder_storage_committed_tail()
            / AUDIO_RECORDER_BYTES_PER_FRAME);
    if (readable_frames == g_audio_recorder_looper_stream_readable_frames) return;
    if (sample_page_cache_update_readable_frames_key(
            g_audio_recorder_looper_stream_key,
            readable_frames) != 0U)
        g_audio_recorder_looper_stream_readable_frames = readable_frames;
}

static uint8_t audio_recorder_publish_start_client_at(
    audio_recorder_client_t client, uint64_t sample_time)
{
    if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder_control_session == 0U))
        return 0U;
    const uint8_t published = control_rt_publish_record(CONTROL_AUDIO_RECORD_START,
        g_audio_recorder.frame_limit, g_audio_recorder_control_session,
        (uint8_t)client, sample_time);
    if (published == 0U)
    {
        Error_Handler();
        return 0U;
    }
    g_audio_recorder.state = AUDIO_RECORDER_STATE_RECORDING;
    return published;
}

static uint8_t audio_recorder_publish_stop_client_at(
    audio_recorder_client_t client, uint64_t sample_time)
{
    if (g_audio_recorder.client != client)
        return 0U;
    if (control_rt_publish_record(CONTROL_AUDIO_RECORD_STOP,
            0U, g_audio_recorder_control_session, (uint8_t)client,
            sample_time) == 0U)
    {
        Error_Handler();
        return 0U;
    }
    return 1U;
}

static uint8_t audio_recorder_copy_path(char *dst, const char *src)
{
    if ((dst == 0) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < AUDIO_RECORDER_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
    }
    dst[0] = '\0';
    return 0U;
}

void audio_recorder_init(void)
{
    memset(&g_audio_recorder, 0, sizeof(g_audio_recorder));
    audio_recorder_storage_init();
    g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
    g_audio_recorder_control_session = 0U;
    audio_recorder_reset_looper_control();
    memset(g_audio_recorder_looper_config, 0,
           sizeof(g_audio_recorder_looper_config));
    g_audio_recorder_looper_admission_open = 1U;
    g_audio_recorder_looper_stream_registered = 0U;
    memset(&g_audio_recorder_looper_stream_key, 0,
           sizeof(g_audio_recorder_looper_stream_key));
    g_audio_recorder_looper_stream_readable_frames = 0U;
}

uint8_t audio_recorder_prepare_client(audio_recorder_client_t client,
                                      const char *temporary_rec_path,
                                      const char *final_wav_path,
                                      uint32_t frame_limit)
{
    if (project_replacement_is_active() != 0U) return 0U;
    if ((client == AUDIO_RECORDER_CLIENT_NONE)
            || (temporary_rec_path == 0) || (final_wav_path == 0)
            || ((client == AUDIO_RECORDER_CLIENT_LOOPER)
                && (g_audio_recorder_looper_admission_open == 0U))
            || ((g_audio_recorder.state != AUDIO_RECORDER_STATE_IDLE)
                && (g_audio_recorder.state != AUDIO_RECORDER_STATE_TAKE_READY)
                && (g_audio_recorder.state != AUDIO_RECORDER_STATE_FAILED)))
    {
        return 0U;
    }
    if ((audio_recorder_copy_path(g_audio_recorder.temporary_path,
                                  temporary_rec_path) == 0U)
            || (audio_recorder_copy_path(g_audio_recorder.final_path,
                                         final_wav_path) == 0U))
    {
        return 0U;
    }
    g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder.client = client;
    g_audio_recorder.frame_limit = (frame_limit != 0U)
        ? frame_limit
        : ((UINT32_MAX - AUDIO_RECORDER_WAV_HEADER_BYTES)
            / AUDIO_RECORDER_BYTES_PER_FRAME);
    if (audio_recorder_storage_prepare(
            g_audio_recorder.temporary_path,
            g_audio_recorder.final_path) == 0U)
    {
        g_audio_recorder.error = audio_recorder_storage_error();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return 0U;
    }
    uint16_t session = (uint16_t)(g_audio_recorder_control_session + 1U);
    if ((session == 0U) || ((session & AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG) != 0U))
        session = 1U;
    g_audio_recorder_control_session = session;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_PREPARED;
    return 1U;
}

uint8_t audio_recorder_start_client_at(audio_recorder_client_t client,
                                       uint64_t sample_time)
{
    if (project_replacement_is_active() != 0U) return 0U;
    return audio_recorder_publish_start_client_at(client, sample_time);
}

uint8_t audio_recorder_cancel_prepared_client(audio_recorder_client_t client)
{
    if ((client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED))
        return 0U;

    if (audio_recorder_storage_cancel() == 0U) return 0U;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
    g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder.client = AUDIO_RECORDER_CLIENT_NONE;
    g_audio_recorder.temporary_path[0] = '\0';
    g_audio_recorder.final_path[0] = '\0';
    if (client == AUDIO_RECORDER_CLIENT_LOOPER)
        audio_recorder_reset_looper_control();
    return 1U;
}

uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client)
{
    uint64_t sample_time = 0U;
    if (!control_rt_now_sample(&sample_time)) return 0U;
    return audio_recorder_publish_stop_client_at(client, sample_time);
}

uint8_t audio_recorder_request_stop_client_at(audio_recorder_client_t client,
                                              uint64_t sample_time)
{
    return audio_recorder_publish_stop_client_at(client, sample_time);
}

static uint8_t audio_recorder_looper_record_eligible(uint8_t track,
                                                     uint8_t *out_overdub)
{
    if ((track_state_get_family(track) != TRACK_FAMILY_SAMPLER)
            || (track_state_get_type(track) != TRACK_TYPE_LOOPER))
        return 0U;
    const uint8_t mode = g_audio_recorder_looper_config[track].arm_mode;
    if ((mode != 1U) && (mode != 2U))
        return 0U;
    uint8_t routed = 0U;
    for (uint8_t source = 0U; source < BRICK_ENTITY_CAPACITY; ++source)
        if ((source != track)
                && (control_routing_get_looper_source(track, source) != 0U))
            routed = 1U;
    if (out_overdub != 0)
        *out_overdub = (mode == 2U) ? 1U : 0U;
    return routed;
}

uint8_t audio_recorder_control_sync_looper_arm(uint8_t rec_armed,
                                               uint32_t samples_per_step_q16)
{
    if (rec_armed == 0U)
    {
        if ((g_audio_recorder.client == AUDIO_RECORDER_CLIENT_LOOPER)
                && (g_audio_recorder.state == AUDIO_RECORDER_STATE_PREPARED))
            return audio_recorder_cancel_prepared_client(
                AUDIO_RECORDER_CLIENT_LOOPER);
        if (audio_recorder_client_is_recording(
                AUDIO_RECORDER_CLIENT_LOOPER) != 0U)
        {
            uint64_t stop_sample = 0U;
            if (control_rt_now_sample(&stop_sample) == 0U)
            {
                Error_Handler();
                return 0U;
            }
            return audio_recorder_control_request_looper_stop(
                stop_sample, 0U);
        }
        return 1U;
    }
    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U)
        return 1U;
    if (audio_recorder_is_active() != 0U)
        return 0U;

    uint8_t selected = 0xFFU;
    uint8_t overdub = 0U;
    uint8_t count = 0U;
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        uint8_t candidate_overdub = 0U;
        if (audio_recorder_looper_record_eligible(track,
                                                  &candidate_overdub) != 0U)
        {
            selected = track;
            overdub = candidate_overdub;
            ++count;
        }
    }
    if (count == 0U)
        return 1U;
    if (count != 1U)
        return 0U;

    const audio_recorder_looper_config_t config =
        g_audio_recorder_looper_config[selected];
    const uint8_t len_mode = config.length_mode;
    uint32_t bars = 0U;
    switch (len_mode)
    {
        case 1U: bars = 1U; break;
        case 2U: bars = 2U; break;
        case 3U: bars = 4U; break;
        case 4U: bars = 8U; break;
        case 5U: bars = 16U; break;
        default: break;
    }
    uint32_t expected_frames = 0U;
    if ((bars != 0U) && (samples_per_step_q16 != 0U))
    {
        uint64_t frames = ((uint64_t)bars
            * AUDIO_RECORDER_LOOPER_STEPS_PER_BAR * samples_per_step_q16
            + 0xFFFFULL) >> 16;
        expected_frames = (frames > UINT32_MAX) ? UINT32_MAX
                                                 : (uint32_t)frames;
    }
    char final_path[LOOPER_STORAGE_PATH_MAX];
    char temporary_path[LOOPER_STORAGE_PATH_MAX];
    if ((looper_storage_make_next_path(selected, final_path,
                                       sizeof(final_path))
            != LOOPER_STORAGE_PATH_OK)
            || (looper_storage_copy_wav_path_as_rec(final_path,
                temporary_path, sizeof(temporary_path)) == 0U)
            || (audio_recorder_prepare_client(AUDIO_RECORDER_CLIENT_LOOPER,
                temporary_path, final_path, expected_frames) == 0U))
        return 0U;

    const uint8_t previous_take_track = g_audio_recorder_looper_take_track;
    g_audio_recorder_looper_take_track = selected;
    g_audio_recorder_looper_take_notified = 0U;
    uint64_t start_sample = 0U;
    if (control_rt_now_sample(&start_sample) == 0U)
    {
        Error_Handler();
        g_audio_recorder_looper_take_track = previous_take_track;
        (void)audio_recorder_cancel_prepared_client(
            AUDIO_RECORDER_CLIENT_LOOPER);
        return 0U;
    }
    if (audio_recorder_control_arm_looper(selected, previous_take_track,
            len_mode, expected_frames,
            config.play_auto, overdub,
            start_sample) == 0U)
    {
        g_audio_recorder_looper_take_track = previous_take_track;
        (void)audio_recorder_cancel_prepared_client(
            AUDIO_RECORDER_CLIENT_LOOPER);
        return 0U;
    }
    return 1U;
}

uint8_t audio_recorder_control_looper_take_track(uint8_t *out_track)
{
    if ((out_track == 0)
            || (g_audio_recorder_looper_take_track >= BRICK_ENTITY_CAPACITY))
        return 0U;
    *out_track = g_audio_recorder_looper_take_track;
    return 1U;
}

void audio_recorder_control_set_looper_admission(uint8_t open)
{
    g_audio_recorder_looper_admission_open = (open != 0U) ? 1U : 0U;
}

uint8_t audio_recorder_control_release_looper_take(void)
{
    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U)
        return 0U;
    if ((g_audio_recorder.client == AUDIO_RECORDER_CLIENT_LOOPER)
            && (g_audio_recorder.state != AUDIO_RECORDER_STATE_TAKE_READY)
            && (g_audio_recorder.state != AUDIO_RECORDER_STATE_FAILED))
        return 0U;
    if (g_audio_recorder_looper_stream_registered != 0U)
    {
        if (sample_page_lease_control_references_key(
                g_audio_recorder_looper_stream_key) != 0U) return 0U;
        audio_recorder_retire_looper_stream();
    }
    g_audio_recorder_looper_take_track = 0xFFU;
    g_audio_recorder_looper_take_notified = 0U;
    if (g_audio_recorder.client == AUDIO_RECORDER_CLIENT_LOOPER)
    {
        audio_recorder_storage_release();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
        g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
        g_audio_recorder.client = AUDIO_RECORDER_CLIENT_NONE;
        g_audio_recorder.temporary_path[0] = '\0';
        g_audio_recorder.final_path[0] = '\0';
    }
    audio_recorder_reset_looper_control();
    return 1U;
}

uint8_t audio_recorder_control_arm_looper(uint8_t track,
                                          uint8_t replace_track,
                                          uint8_t len_mode,
                                          uint32_t expected_frames,
                                          uint8_t play_auto,
                                          uint8_t overdub,
                                          uint64_t request_sample)
{
    if (project_replacement_is_active() != 0U) return 0U;
    if ((g_audio_recorder.client != AUDIO_RECORDER_CLIENT_LOOPER)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder_looper_admission_open == 0U))
        return 0U;
    const uint16_t replace_config = (replace_track < BRICK_ENTITY_CAPACITY)
        ? (uint16_t)(AUDIO_RECORDER_LOOPER_REPLACE_VALID_FLAG
            | ((uint16_t)replace_track
                << AUDIO_RECORDER_LOOPER_REPLACE_TRACK_SHIFT)) : 0U;
    const uint16_t config = (uint16_t)(AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG
        | ((uint16_t)(overdub != 0U) * AUDIO_RECORDER_LOOPER_OVERDUB_FLAG)
        | replace_config | len_mode
        | ((uint16_t)(play_auto != 0U)
            << AUDIO_RECORDER_LOOPER_PLAY_AUTO_SHIFT));
    if (control_rt_publish_record(CONTROL_AUDIO_RECORD_START,
            expected_frames, config, track, request_sample) == 0U)
    {
        Error_Handler();
        return 0U;
    }
    g_audio_recorder_looper_control = (audio_recorder_looper_control_t){
        .expected_frames = expected_frames,
        .track = track,
        .start_armed = 1U,
        .overdub = (overdub != 0U) ? 1U : 0U
    };
    return 1U;
}

uint8_t audio_recorder_control_request_looper_stop(uint64_t request_sample,
                                                   uint8_t wait_boundary)
{
    audio_recorder_looper_control_t *const control =
        &g_audio_recorder_looper_control;
    if ((control->start_armed == 0U) && (control->recording == 0U))
        return 0U;
    const uint16_t config = AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG;
    if ((control->recording != 0U) && (wait_boundary == 0U))
    {
        if (audio_recorder_publish_stop_client_at(
                AUDIO_RECORDER_CLIENT_LOOPER, request_sample) == 0U)
            return 0U;
        control->recording = 0U;
        control->stop_armed = 0U;
        return 1U;
    }
    if (control_rt_publish_record(CONTROL_AUDIO_RECORD_STOP,
            0U, config, control->track, request_sample) == 0U)
    {
        Error_Handler();
        return 0U;
    }
    if (control->recording == 0U)
    {
        audio_recorder_reset_looper_control();
        return 1U;
    }
    control->stop_armed = 1U;
    if (wait_boundary != 0U)
        return 1U;
    return 0U;
}

void audio_recorder_control_on_looper_boundary(uint8_t track,
                                               uint64_t sample_time)
{
    audio_recorder_looper_control_t *const control =
        &g_audio_recorder_looper_control;
    if (track != control->track)
        return;
    if ((control->start_armed == 0U) && (control->recording == 0U))
        return;
    if (control->start_armed != 0U)
    {
        if (audio_recorder_publish_start_client_at(
                AUDIO_RECORDER_CLIENT_LOOPER, sample_time) == 0U)
            return;
        control->start_armed = 0U;
        control->recording = 1U;
        control->actual_start_sample = sample_time;
        control->target_stop_sample = (control->expected_frames != 0U)
            ? sample_time + control->expected_frames : 0U;
        g_audio_recorder_looper_config[control->track].arm_mode = 0U;
    }
    if ((control->recording != 0U)
            && (((control->target_stop_sample != 0U)
                    && (sample_time >= control->target_stop_sample)
                    && (sample_time > control->actual_start_sample))
                || (control->stop_armed != 0U)))
    {
        if (audio_recorder_publish_stop_client_at(
                AUDIO_RECORDER_CLIENT_LOOPER, sample_time) == 0U)
            return;
        control->recording = 0U;
        control->stop_armed = 0U;
    }
}

void audio_recorder_control_on_transport_start(uint64_t sample_time)
{
    if (g_audio_recorder_looper_control.track < BRICK_ENTITY_CAPACITY)
        audio_recorder_control_on_looper_boundary(
            g_audio_recorder_looper_control.track, sample_time);
}

void audio_recorder_service(void)
{
    audio_recorder_storage_service(
        g_audio_recorder_control_session,
        (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING));
    const audio_recorder_storage_phase_t storage_phase =
        audio_recorder_storage_phase();
    if (storage_phase == AUDIO_RECORDER_STORAGE_FAILED)
    {
        if (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            if (audio_recorder_request_stop_client(g_audio_recorder.client) == 0U)
                Error_Handler();
        g_audio_recorder.error = audio_recorder_storage_error();
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
    }
    else if ((storage_phase == AUDIO_RECORDER_STORAGE_DRAINING)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING))
    {
        g_audio_recorder.error = audio_recorder_storage_error();
        audio_recorder_enter_draining();
    }
    else if ((storage_phase == AUDIO_RECORDER_STORAGE_FINALIZING)
            && (g_audio_recorder.state != AUDIO_RECORDER_STATE_FINALIZING))
    {
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FINALIZING;
    }
    else if (storage_phase == AUDIO_RECORDER_STORAGE_TAKE_READY)
    {
        if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_TAKE_READY)
                && (g_audio_recorder.client == AUDIO_RECORDER_CLIENT_LOOPER)
                && (g_audio_recorder_looper_stream_registered != 0U))
            (void)sample_page_cache_update_stream_path_key(
                g_audio_recorder_looper_stream_key,
                g_audio_recorder.final_path);
        g_audio_recorder.state = AUDIO_RECORDER_STATE_TAKE_READY;
    }
    audio_recorder_update_looper_stream_readable();
    if ((g_audio_recorder.client == AUDIO_RECORDER_CLIENT_LOOPER)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY)
            && (g_audio_recorder.error == AUDIO_RECORDER_ERROR_NONE)
            && (g_audio_recorder_looper_take_notified == 0U))
    {
        const char *path = 0;
        uint32_t frames = 0U;
        if (audio_recorder_get_last_take_client(AUDIO_RECORDER_CLIENT_LOOPER,
                                                &path, &frames) != 0U)
        {
            (void)wav_loader_catalog_notify_file_created(path);
            (void)waveform_cache_request_for_wav_known_duration(
                path, WAVEFORM_CACHE_REASON_POST_LOOPER_SAVE, frames,
                AUDIO_RECORDER_SAMPLE_RATE_HZ);
            g_audio_recorder_looper_take_notified = 1U;
        }
    }
}

uint8_t audio_recorder_get_status_client(audio_recorder_client_t client,
                                         audio_recorder_status_t *status)
{
    if ((status == 0) || (client == AUDIO_RECORDER_CLIENT_NONE)
            || ((g_audio_recorder.client != AUDIO_RECORDER_CLIENT_NONE)
                && (g_audio_recorder.client != client)))
        return 0U;
    memset(status, 0, sizeof(*status));
    generic_recorder_status_t generic_status;
    audio_recorder_storage_get_status(&generic_status);
    audio_recorder_metrics_t metrics;
    audio_recorder_storage_get_metrics(&metrics);
    status->state = g_audio_recorder.state;
    status->error = g_audio_recorder.error;
    status->frames_received = g_audio_recorder_capture.head_cursor;
    status->frames_assigned = (uint32_t)(
        generic_status.assigned_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_committed = (uint32_t)(
        generic_status.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_pending = status->frames_received - status->frames_committed;
    status->high_watermark =
        metrics.recorder.ring_high_watermark_frames;
    status->overflow_count = metrics.recorder.ring_full_rejects;
    status->dropped_frames = 0U;
    return 1U;
}

uint8_t audio_recorder_get_last_take_client(audio_recorder_client_t client,
                                            const char **path,
                                            uint32_t *frames)
{
    if ((g_audio_recorder.client != client) || (path == 0) || (frames == 0)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_TAKE_READY))
        return 0U;
    *path = g_audio_recorder.final_path;
    *frames = (uint32_t)(audio_recorder_storage_committed_tail()
                         / AUDIO_RECORDER_BYTES_PER_FRAME);
    return (*frames != 0U) ? 1U : 0U;
}

void audio_recorder_get_metrics(audio_recorder_metrics_t *metrics)
{
    if (metrics == 0) return;
    audio_recorder_storage_get_metrics(metrics);
}

uint8_t audio_recorder_is_active(void)
{
    return ((g_audio_recorder.state == AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING))
        ? 1U : 0U;
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

uint8_t audio_recorder_looper_take_resource_retained(void)
{
    return g_audio_recorder_looper_stream_registered;
}
