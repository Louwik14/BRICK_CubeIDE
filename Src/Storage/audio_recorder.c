#include "Storage/audio_recorder.h"

#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/generic_recorder_adapters.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "IPC/live_clock_control.h"
#include "Param/param_registry.h"
#include "Storage/looper_storage.h"
#include "Storage/wav_loader.h"
#include "Storage/waveform_cache.h"
#include "Track/control_routing.h"
#include "Track/track_state.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define AUDIO_RECORDER_WRITE_BUFFER_BYTES (32768U)
#define AUDIO_RECORDER_MINIMUM_WRITE_BYTES (8192U)
#define AUDIO_RECORDER_INITIAL_RESERVE_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_EXTENSION_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_RESERVATION_LOW_US (3000000U)
#define AUDIO_RECORDER_RESERVATION_CRITICAL_US (1000000U)
#define AUDIO_RECORDER_LOOPER_STEPS_PER_BAR 16U

typedef enum
{
    AUDIO_RECORDER_FINAL_NONE = 0,
    AUDIO_RECORDER_FINAL_COMMIT,
    AUDIO_RECORDER_FINAL_RELEASE,
    AUDIO_RECORDER_FINAL_HEADER,
    AUDIO_RECORDER_FINAL_SYNC,
    AUDIO_RECORDER_FINAL_CLOSE,
    AUDIO_RECORDER_FINAL_RENAME,
    AUDIO_RECORDER_FINAL_DONE
} audio_recorder_final_phase_t;

typedef struct
{
    generic_recorder_t recorder;
    recorder_file_reservation_t reservation;
    sd_scheduler_provider_t recorder_filesystem_provider;
    audio_recorder_metrics_t metrics;
    audio_recorder_state_t state;
    audio_recorder_error_t error;
    audio_recorder_final_phase_t final_phase;
    audio_recorder_client_t client;
    uint32_t frame_limit;
    uint32_t final_started_ms;
    char temporary_path[AUDIO_RECORDER_PATH_MAX];
    char final_path[AUDIO_RECORDER_PATH_MAX];
} audio_recorder_runtime_t;

/* FatFs, callbacks and generic-recorder pointers are strictly M4-private. */
STORAGE_STATE_SDRAM static audio_recorder_runtime_t g_audio_recorder;
RECORDER_SCRATCH_SDRAM static uint8_t
    g_audio_recorder_write_buffers[GENERIC_RECORDER_WRITE_BUFFER_COUNT]
                                  [AUDIO_RECORDER_WRITE_BUFFER_BYTES];

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
static uint8_t g_audio_recorder_looper_take_track = 0xFFU;
static uint8_t g_audio_recorder_looper_take_notified;

static uint8_t audio_recorder_publish_start_client_at(
    audio_recorder_client_t client, uint64_t sample_time)
{
    if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder_control_session == 0U))
        return 0U;
    const uint8_t published = control_audio_publish_record(CONTROL_AUDIO_RECORD_START,
        g_audio_recorder.frame_limit, g_audio_recorder_control_session,
        (uint8_t)client, sample_time);
    if (published != 0U)
        g_audio_recorder.state = AUDIO_RECORDER_STATE_RECORDING;
    return published;
}

static uint8_t audio_recorder_publish_stop_client_at(
    audio_recorder_client_t client, uint64_t sample_time)
{
    if (g_audio_recorder.client != client)
        return 0U;
    return control_audio_publish_record(CONTROL_AUDIO_RECORD_STOP,
        0U, g_audio_recorder_control_session, (uint8_t)client,
        sample_time);
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

static audio_recorder_error_t audio_recorder_map_error(
    generic_recorder_error_t error)
{
    if (error == GENERIC_RECORDER_ERROR_RING_FULL)
        return AUDIO_RECORDER_ERROR_RING_OVERFLOW;
    if (error == GENERIC_RECORDER_ERROR_NO_SPACE)
        return AUDIO_RECORDER_ERROR_NO_SPACE;
    if (error == GENERIC_RECORDER_ERROR_MEDIA_CHANGED)
        return AUDIO_RECORDER_ERROR_MEDIA_CHANGED;
    if (error == GENERIC_RECORDER_ERROR_NONE)
        return AUDIO_RECORDER_ERROR_NONE;
    return AUDIO_RECORDER_ERROR_SD_IO;
}

static uint8_t audio_recorder_filesystem_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    audio_recorder_runtime_t *const runtime = context;
    if ((runtime == 0) || (candidate == 0))
    {
        return 0U;
    }
    if ((runtime->recorder_filesystem_provider.peek != 0)
            && (runtime->recorder_filesystem_provider.peek(
                    runtime->recorder_filesystem_provider.context,
                    candidate) != 0U))
    {
        return 1U;
    }
    if ((runtime->state != AUDIO_RECORDER_STATE_FINALIZING)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_NONE)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_DONE))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_FILESYSTEM;
    candidate->ready = 1U;
    candidate->margin_us = SD_SCHEDULER_MARGIN_UNKNOWN;
    candidate->estimated_cost_us = 100000U;
    candidate->owner_generation = runtime->recorder.generation;
    candidate->media_epoch = runtime->recorder.media_epoch;
    candidate->reservation = SD_SCHEDULER_RESERVATION_SAFE;
    return 1U;
}

static sd_scheduler_start_result_t audio_recorder_finalization_step(
    audio_recorder_runtime_t *runtime)
{
    const uint32_t started = HAL_GetTick();
    recorder_file_reservation_result_t reservation_result;
    FRESULT fr;
    UINT written;
    uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES];
    switch (runtime->final_phase)
    {
        case AUDIO_RECORDER_FINAL_COMMIT:
            reservation_result = recorder_file_reservation_commit_valid(
                &runtime->reservation, runtime->recorder.committed_tail);
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_RELEASE;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_RELEASE:
            reservation_result = recorder_file_reservation_release_unused(
                &runtime->reservation);
            runtime->metrics.release_duration_us =
                (HAL_GetTick() - started) * 1000U;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_HEADER;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_HEADER:
            if ((runtime->recorder.committed_tail > UINT32_MAX)
                    || (audio_recorder_wav_build_header(
                            header, (uint32_t)runtime->recorder.committed_tail,
                            AUDIO_RECORDER_SAMPLE_RATE_HZ,
                            AUDIO_RECORDER_CHANNELS) == 0U))
                return SD_SCHEDULER_START_ERROR;
            written = 0U;
            fr = f_lseek(&runtime->reservation.file, 0U);
            if (fr == FR_OK)
                fr = f_write(&runtime->reservation.file, header,
                             sizeof(header), &written);
            runtime->metrics.header_duration_us =
                (HAL_GetTick() - started) * 1000U;
            if ((fr != FR_OK) || (written != sizeof(header)))
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_SYNC;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_SYNC:
            fr = f_sync(&runtime->reservation.file);
            runtime->metrics.sync_duration_us =
                (HAL_GetTick() - started) * 1000U;
            if (fr != FR_OK) return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_CLOSE;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_CLOSE:
            reservation_result = recorder_file_reservation_close(
                &runtime->reservation);
            runtime->metrics.close_duration_us =
                (HAL_GetTick() - started) * 1000U;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_RENAME;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_RENAME:
            reservation_result = recorder_file_reservation_rename_closed(
                &runtime->reservation, runtime->final_path);
            runtime->metrics.rename_duration_us =
                (HAL_GetTick() - started) * 1000U;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_DONE;
            runtime->metrics.finalization_duration_us =
                (HAL_GetTick() - runtime->final_started_ms) * 1000U;
            runtime->state = AUDIO_RECORDER_STATE_TAKE_READY;
            return SD_SCHEDULER_START_COMPLETED;

        default:
            return SD_SCHEDULER_START_ERROR;
    }
}

static sd_scheduler_start_result_t audio_recorder_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    audio_recorder_runtime_t *const runtime = context;
    if ((runtime == 0) || (candidate == 0)
            || (candidate->owner_generation != runtime->recorder.generation))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    if ((runtime->recorder_filesystem_provider.peek != 0)
            && (runtime->recorder_filesystem_provider.peek(
                    runtime->recorder_filesystem_provider.context,
                    &(sd_scheduler_candidate_t){0}) != 0U))
    {
        return runtime->recorder_filesystem_provider.start(
            runtime->recorder_filesystem_provider.context,
            candidate, granted_sector_count);
    }
    const sd_scheduler_start_result_t result =
        audio_recorder_finalization_step(runtime);
    if (result == SD_SCHEDULER_START_ERROR)
    {
        runtime->error = AUDIO_RECORDER_ERROR_SD_IO;
        runtime->state = AUDIO_RECORDER_STATE_FAILED;
    }
    return result;
}

void audio_recorder_init(void)
{
    memset(&g_audio_recorder, 0, sizeof(g_audio_recorder));
    generic_recorder_init(&g_audio_recorder.recorder);
    recorder_file_reservation_init(&g_audio_recorder.reservation);
    g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
    g_audio_recorder_capture.tail_cursor = 0U;
    g_audio_recorder_control_session = 0U;
    memset(&g_audio_recorder_looper_control, 0,
           sizeof(g_audio_recorder_looper_control));
    g_audio_recorder_looper_control.track = 0xFFU;
    const sd_scheduler_provider_t write_provider =
        generic_recorder_write_provider(&g_audio_recorder.recorder);
    g_audio_recorder.recorder_filesystem_provider =
        generic_recorder_filesystem_provider(&g_audio_recorder.recorder);
    const sd_scheduler_provider_t filesystem_provider = {
        .context = &g_audio_recorder,
        .peek = audio_recorder_filesystem_peek,
        .start = audio_recorder_filesystem_start,
        .poll = 0,
    };
    (void)sd_scheduler_runtime_bind_recorder(
        &write_provider, &filesystem_provider);
}

uint8_t audio_recorder_prepare_client(audio_recorder_client_t client,
                                      const char *temporary_rec_path,
                                      const char *final_wav_path,
                                      uint32_t frame_limit)
{
    if ((client == AUDIO_RECORDER_CLIENT_NONE)
            || (temporary_rec_path == 0) || (final_wav_path == 0)
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
    memset(&g_audio_recorder.metrics, 0, sizeof(g_audio_recorder.metrics));
    generic_recorder_init(&g_audio_recorder.recorder);
    recorder_file_reservation_init(&g_audio_recorder.reservation);
    g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder.final_phase = AUDIO_RECORDER_FINAL_NONE;
    g_audio_recorder.client = client;
    g_audio_recorder.frame_limit = (frame_limit != 0U)
        ? frame_limit
        : ((UINT32_MAX - AUDIO_RECORDER_WAV_HEADER_BYTES)
            / AUDIO_RECORDER_BYTES_PER_FRAME);

    if (sd_access_gate_try_acquire(
            SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
    {
        g_audio_recorder.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return 0U;
    }
    (void)f_unlink(g_audio_recorder.temporary_path);
    (void)f_unlink(g_audio_recorder.final_path);
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
    const recorder_file_reservation_result_t created =
        recorder_file_reservation_create(
            &g_audio_recorder.reservation,
            g_audio_recorder.temporary_path,
            AUDIO_RECORDER_WAV_HEADER_BYTES,
            AUDIO_RECORDER_INITIAL_RESERVE_BYTES);
    if ((created != RECORDER_FILE_RESERVATION_OK)
            && (created != RECORDER_FILE_RESERVATION_PARTIAL))
    {
        g_audio_recorder.error = (created == RECORDER_FILE_RESERVATION_NO_SPACE)
            ? AUDIO_RECORDER_ERROR_NO_SPACE : AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return 0U;
    }
    generic_recorder_config_t config;
    memset(&config, 0, sizeof(config));
    config.ring_interleaved = g_audio_recorder_capture_ring;
    config.ring_capacity_frames = AUDIO_RECORDER_CAPTURE_RING_FRAMES;
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
        config.write_buffers[i] = g_audio_recorder_write_buffers[i];
    config.write_buffer_bytes = AUDIO_RECORDER_WRITE_BUFFER_BYTES;
    config.minimum_write_bytes = AUDIO_RECORDER_MINIMUM_WRITE_BYTES;
    config.sample_rate_hz = AUDIO_RECORDER_SAMPLE_RATE_HZ;
    config.channels = AUDIO_RECORDER_CHANNELS;
    config.reserved_header_bytes = AUDIO_RECORDER_WAV_HEADER_BYTES;
    config.extension_bytes = AUDIO_RECORDER_EXTENSION_BYTES;
    config.reservation_low_margin_us = AUDIO_RECORDER_RESERVATION_LOW_US;
    config.reservation_critical_margin_us =
        AUDIO_RECORDER_RESERVATION_CRITICAL_US;
    config.estimated_write_us_per_sector = 250U;
    /* The generic recorder is M4-owned. M7 publishes only capture head/stop
     * through g_audio_recorder_capture, so a local IRQ mask is neither needed
     * nor a valid inter-core lock. */
    config.critical_enter = 0;
    config.critical_exit = 0;
    config.transport = generic_recorder_sd_block_device_adapter();
    config.reservation = generic_recorder_fatfs_reservation_adapter(
        &g_audio_recorder.reservation);
    if (generic_recorder_begin(&g_audio_recorder.recorder, &config) == 0U)
    {
        g_audio_recorder.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return 0U;
    }
    uint16_t session = (uint16_t)(g_audio_recorder_control_session + 1U);
    if ((session == 0U) || ((session & AUDIO_RECORDER_LOOPER_RECORD_ID_FLAG) != 0U))
        session = 1U;
    g_audio_recorder_control_session = session;
    g_audio_recorder_capture.tail_cursor = 0U;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_PREPARED;
    return 1U;
}

uint8_t audio_recorder_start_client_at(audio_recorder_client_t client,
                                       uint64_t sample_time)
{
    return audio_recorder_publish_start_client_at(client, sample_time);
}

uint8_t audio_recorder_cancel_prepared_client(audio_recorder_client_t client)
{
    if ((client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED))
        return 0U;

    if (recorder_file_reservation_close(&g_audio_recorder.reservation)
            != RECORDER_FILE_RESERVATION_OK)
        return 0U;
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
        return 0U;
    (void)f_unlink(g_audio_recorder.temporary_path);
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);

    generic_recorder_init(&g_audio_recorder.recorder);
    recorder_file_reservation_init(&g_audio_recorder.reservation);
    g_audio_recorder.state = AUDIO_RECORDER_STATE_IDLE;
    g_audio_recorder.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder.client = AUDIO_RECORDER_CLIENT_NONE;
    g_audio_recorder.temporary_path[0] = '\0';
    g_audio_recorder.final_path[0] = '\0';
    if (client == AUDIO_RECORDER_CLIENT_LOOPER)
    {
        memset(&g_audio_recorder_looper_control, 0,
               sizeof(g_audio_recorder_looper_control));
        g_audio_recorder_looper_control.track = 0xFFU;
    }
    return 1U;
}

uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client)
{
    uint64_t sample_time = 0U;
    if (!live_clock_read_audio_sample(&sample_time)) return 0U;
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
    if ((track_state_get_family(track) != UI_TRACK_FAMILY_SAMPLER)
            || (track_state_get_type(track) != UI_TRACK_TYPE_LOOPER))
        return 0U;
    float arm = 0.0f;
    if (param_registry_get_track_value(PARAM_LOOPER_ARM, track, &arm) == 0U)
        return 0U;
    const uint8_t mode = (uint8_t)(arm + 0.5f);
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
            return audio_recorder_control_request_looper_stop(
                live_clock_control_sample(), 0U);
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

    float len_value = 0.0f;
    float play_value = 0.0f;
    (void)param_registry_get_track_value(PARAM_LOOPER_LEN, selected,
                                         &len_value);
    (void)param_registry_get_track_value(PARAM_LOOPER_PLAY, selected,
                                         &play_value);
    const uint8_t len_mode = (uint8_t)(len_value + 0.5f);
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
    if (audio_recorder_control_arm_looper(selected, previous_take_track,
            len_mode, expected_frames,
            ((uint8_t)(play_value + 0.5f) == 1U), overdub,
            live_clock_control_sample()) == 0U)
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

uint8_t audio_recorder_control_arm_looper(uint8_t track,
                                          uint8_t replace_track,
                                          uint8_t len_mode,
                                          uint32_t expected_frames,
                                          uint8_t play_auto,
                                          uint8_t overdub,
                                          uint64_t request_sample)
{
    if ((g_audio_recorder.client != AUDIO_RECORDER_CLIENT_LOOPER)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED))
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
    if (control_audio_publish_record(CONTROL_AUDIO_RECORD_START,
            expected_frames, config, track, request_sample) == 0U)
        return 0U;
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
    if (control_audio_publish_record(CONTROL_AUDIO_RECORD_STOP,
            0U, config, control->track, request_sample) == 0U)
        return 0U;
    if (control->recording == 0U)
    {
        memset(control, 0, sizeof(*control));
        control->track = 0xFFU;
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
        (void)param_registry_apply_track_value(PARAM_LOOPER_ARM,
                                                control->track, 0.0f);
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

static void audio_recorder_capture_transport_service(void)
{
    if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_RECORDING)
            && (g_audio_recorder.state != AUDIO_RECORDER_STATE_DRAINING)) return;
    const uint32_t accepted_frames = g_audio_recorder_capture.head_cursor;
    __DMB();
    const uint64_t accepted_tail =
        (uint64_t)accepted_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
    g_audio_recorder.recorder.accepted_frames = accepted_frames;
    g_audio_recorder.recorder.accepted_tail = accepted_tail;
    g_audio_recorder.recorder.metrics.frames_accepted = accepted_frames;
    g_audio_recorder.recorder.metrics.bytes_accepted = accepted_tail;
    const uint32_t committed_frames = (uint32_t)(
        g_audio_recorder.recorder.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    __DMB();
    g_audio_recorder_capture.tail_cursor = committed_frames;
    const uint32_t retained = accepted_frames - committed_frames;
    if (retained > g_audio_recorder.recorder.metrics.ring_high_watermark_frames)
        g_audio_recorder.recorder.metrics.ring_high_watermark_frames = retained;
    const uint32_t free_frames = AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained;
    if (free_frames < g_audio_recorder.recorder.metrics.ring_min_free_frames)
        g_audio_recorder.recorder.metrics.ring_min_free_frames = free_frames;
    const uint64_t backlog = accepted_tail
        - g_audio_recorder.recorder.committed_tail;
    if (backlog > g_audio_recorder.recorder.metrics.max_backlog_bytes)
        g_audio_recorder.recorder.metrics.max_backlog_bytes = backlog;

    if (g_audio_recorder_capture.capture_fault != AUDIO_RECORDER_ERROR_NONE)
    {
        g_audio_recorder.error =
            (audio_recorder_error_t)g_audio_recorder_capture.capture_fault;
        g_audio_recorder.recorder.error = GENERIC_RECORDER_ERROR_RING_FULL;
    }
    if ((g_audio_recorder_capture.closed_session
            == g_audio_recorder_control_session)
            && (g_audio_recorder.recorder.state == GENERIC_RECORDER_CAPTURING))
    {
        (void)generic_recorder_request_stop(
            &g_audio_recorder.recorder, HAL_GetTick() * 1000U);
        g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
    }
}

void audio_recorder_service(void)
{
    audio_recorder_capture_transport_service();
    if (sd_scheduler_runtime_owner() == SD_SCHEDULER_OWNER_WRITE_DMA)
        g_audio_recorder.metrics.superloop_iterations_during_write++;
    generic_recorder_service(
        &g_audio_recorder.recorder, HAL_GetTick() * 1000U);
    if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_ERROR)
            || (g_audio_recorder.recorder.state == GENERIC_RECORDER_ABORTED))
    {
        if (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            (void)audio_recorder_request_stop_client(g_audio_recorder.client);
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
    }
    else if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_DRAINING)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING))
    {
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        if (g_audio_recorder.error != AUDIO_RECORDER_ERROR_NONE)
        {
        }
        g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
    }
    else if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_FINALIZABLE)
            && (g_audio_recorder.final_phase == AUDIO_RECORDER_FINAL_NONE))
    {
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FINALIZING;
        g_audio_recorder.final_phase = AUDIO_RECORDER_FINAL_COMMIT;
        g_audio_recorder.final_started_ms = HAL_GetTick();
    }
    sd_scheduler_runtime_service();
    generic_recorder_service(
        &g_audio_recorder.recorder, HAL_GetTick() * 1000U);
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
    generic_recorder_get_status(&g_audio_recorder.recorder, &generic_status);
    status->state = g_audio_recorder.state;
    status->error = g_audio_recorder.error;
    status->frames_received = g_audio_recorder_capture.head_cursor;
    status->frames_assigned = (uint32_t)(
        generic_status.assigned_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_committed = (uint32_t)(
        generic_status.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    status->frames_pending = status->frames_received - status->frames_committed;
    status->high_watermark =
        g_audio_recorder.recorder.metrics.ring_high_watermark_frames;
    status->overflow_count = g_audio_recorder.recorder.metrics.ring_full_rejects;
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
    *frames = (uint32_t)(g_audio_recorder.recorder.committed_tail
                         / AUDIO_RECORDER_BYTES_PER_FRAME);
    return (*frames != 0U) ? 1U : 0U;
}

void audio_recorder_get_metrics(audio_recorder_metrics_t *metrics)
{
    if (metrics == 0) return;
    g_audio_recorder.metrics.recorder = g_audio_recorder.recorder.metrics;
    g_audio_recorder.metrics.reservation = g_audio_recorder.reservation.metrics;
    sd_scheduler_runtime_metrics_get(&g_audio_recorder.metrics.scheduler);
    sd_block_device_async_metrics_get(&g_audio_recorder.metrics.block_device);
    *metrics = g_audio_recorder.metrics;
}

uint8_t audio_recorder_is_active(void)
{
    return ((g_audio_recorder.state == AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING))
        ? 1U : 0U;
}

uint8_t audio_recorder_get_live_stream(audio_recorder_client_t client,
                                       audio_recorder_live_stream_t *stream)
{
    if ((stream == 0) || (client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder.state < AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FAILED)
            || (recorder_file_reservation_map_snapshot_owned(
                &g_audio_recorder.reservation, &stream->reservation) == 0U)) return 0U;
    stream->accepted_frames = g_audio_recorder_capture.head_cursor;
    stream->committed_frames = (uint32_t)(g_audio_recorder.recorder.committed_tail
        / AUDIO_RECORDER_BYTES_PER_FRAME);
    const char *const path = (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY)
        ? g_audio_recorder.final_path : g_audio_recorder.temporary_path;
    return audio_recorder_copy_path(stream->path, path);
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
