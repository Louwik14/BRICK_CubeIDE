#include "Storage/audio_recorder_storage.h"

#include <stdio.h>
#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/looper_storage.h"
#include "Storage/generic_recorder_adapters.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "Storage/storage_io_wakeup.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "main.h"

#define AUDIO_RECORDER_WRITE_BUFFER_BYTES (32768U)
#define AUDIO_RECORDER_MINIMUM_WRITE_BYTES (8192U)
#define AUDIO_RECORDER_INITIAL_RESERVE_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_EXTENSION_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_RESERVATION_LOW_US (3000000U)
#define AUDIO_RECORDER_RESERVATION_CRITICAL_US (1000000U)
#define AUDIO_RECORDER_AUDIO_REC_TEMP_PATH "0:/PROJECT/REC/AUDIOREC_TMP.REC"
#define AUDIO_RECORDER_AUDIO_REC_WAV_PATH "0:/PROJECT/REC/AUDIOREC_TMP.WAV"

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
    audio_recorder_storage_phase_t phase;
    audio_recorder_error_t error;
    audio_recorder_final_phase_t final_phase;
    uint32_t final_started_ms;
    char temporary_path[AUDIO_RECORDER_PATH_MAX];
    char final_path[AUDIO_RECORDER_PATH_MAX];
    uint8_t prepare_pending;
    uint8_t prepare_client;
    uint8_t prepare_looper_track;
    uint8_t cancel_requested;
    uint32_t prepare_frame_limit;
    uint32_t prepare_session_id;
} audio_recorder_storage_runtime_t;

/* FatFs, callbacks, generic-recorder state and DMA buffers are STORAGE-only. */
STORAGE_STATE_SDRAM static audio_recorder_storage_runtime_t g_audio_recorder_storage;
RECORDER_SCRATCH_SDRAM static uint8_t
    g_audio_recorder_write_buffers[GENERIC_RECORDER_WRITE_BUFFER_COUNT]
                                  [AUDIO_RECORDER_WRITE_BUFFER_BYTES];

static audio_recorder_error_t audio_recorder_storage_map_error(
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

static uint8_t audio_recorder_storage_filesystem_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    audio_recorder_storage_runtime_t *const runtime = context;
    if ((runtime == 0) || (candidate == 0)) return 0U;
    if ((runtime->recorder_filesystem_provider.peek != 0)
            && (runtime->recorder_filesystem_provider.peek(
                    runtime->recorder_filesystem_provider.context,
                    candidate) != 0U))
        return 1U;
    if ((runtime->phase != AUDIO_RECORDER_STORAGE_FINALIZING)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_NONE)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_DONE))
        return 0U;
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

static sd_scheduler_start_result_t audio_recorder_storage_finalization_step(
    audio_recorder_storage_runtime_t *runtime)
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
            runtime->phase = AUDIO_RECORDER_STORAGE_TAKE_READY;
            return SD_SCHEDULER_START_COMPLETED;

        default:
            return SD_SCHEDULER_START_ERROR;
    }
}

static sd_scheduler_start_result_t audio_recorder_storage_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    audio_recorder_storage_runtime_t *const runtime = context;
    if ((runtime == 0) || (candidate == 0)
            || (candidate->owner_generation != runtime->recorder.generation))
        return SD_SCHEDULER_START_ERROR;
    if ((runtime->recorder_filesystem_provider.peek != 0)
            && (runtime->recorder_filesystem_provider.peek(
                    runtime->recorder_filesystem_provider.context,
                    &(sd_scheduler_candidate_t){0}) != 0U))
        return runtime->recorder_filesystem_provider.start(
            runtime->recorder_filesystem_provider.context,
            candidate, granted_sector_count);
    const sd_scheduler_start_result_t result =
        audio_recorder_storage_finalization_step(runtime);
    if (result == SD_SCHEDULER_START_ERROR)
    {
        runtime->error = AUDIO_RECORDER_ERROR_SD_IO;
        runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
    }
    return result;
}

void audio_recorder_storage_init(void)
{
    memset(&g_audio_recorder_storage, 0,
           sizeof(g_audio_recorder_storage));
    generic_recorder_init(&g_audio_recorder_storage.recorder);
    g_audio_recorder_capture.tail_cursor = 0U;
    recorder_file_reservation_init(&g_audio_recorder_storage.reservation);
    g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_IDLE;
    const sd_scheduler_provider_t write_provider =
        generic_recorder_write_provider(&g_audio_recorder_storage.recorder);
    g_audio_recorder_storage.recorder_filesystem_provider =
        generic_recorder_filesystem_provider(&g_audio_recorder_storage.recorder);
    const sd_scheduler_provider_t filesystem_provider = {
        .context = &g_audio_recorder_storage,
        .peek = audio_recorder_storage_filesystem_peek,
        .start = audio_recorder_storage_filesystem_start,
        .poll = 0,
    };
    (void)sd_scheduler_runtime_bind_recorder(
        &write_provider, &filesystem_provider);
}

uint8_t audio_recorder_storage_prepare_request(audio_recorder_client_t client,
                                                uint8_t looper_track,
                                                uint32_t frame_limit,
                                                uint32_t session_id)
{
    audio_recorder_storage_runtime_t *const runtime =
        &g_audio_recorder_storage;
    if ((client == AUDIO_RECORDER_CLIENT_NONE)
            || (runtime->prepare_pending != 0U)
            || (runtime->phase != AUDIO_RECORDER_STORAGE_IDLE
                && runtime->phase != AUDIO_RECORDER_STORAGE_TAKE_READY
                && runtime->phase != AUDIO_RECORDER_STORAGE_FAILED))
        return 0U;
    runtime->prepare_client = (uint8_t)client;
    runtime->prepare_looper_track = looper_track;
    runtime->prepare_frame_limit = frame_limit;
    runtime->prepare_session_id = session_id;
    runtime->cancel_requested = 0U;
    __DMB();
    runtime->prepare_pending = 1U;
    storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
    return 1U;
}

static uint8_t audio_recorder_storage_prepare_physical(
    const char *temporary_rec_path, const char *final_wav_path)
{
    if ((temporary_rec_path == 0) || (final_wav_path == 0)) return 0U;
    if ((strlen(temporary_rec_path) >= AUDIO_RECORDER_PATH_MAX)
            || (strlen(final_wav_path) >= AUDIO_RECORDER_PATH_MAX)) return 0U;
    (void)strcpy(g_audio_recorder_storage.temporary_path, temporary_rec_path);
    (void)strcpy(g_audio_recorder_storage.final_path, final_wav_path);
    memset(&g_audio_recorder_storage.metrics, 0,
           sizeof(g_audio_recorder_storage.metrics));
    generic_recorder_init(&g_audio_recorder_storage.recorder);
    recorder_file_reservation_init(&g_audio_recorder_storage.reservation);
    g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_storage.final_phase = AUDIO_RECORDER_FINAL_NONE;

    if (sd_access_gate_try_acquire(
            SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
    {
        g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_FAILED;
        return 0U;
    }
    (void)f_unlink(g_audio_recorder_storage.temporary_path);
    (void)f_unlink(g_audio_recorder_storage.final_path);
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
    const recorder_file_reservation_result_t created =
        recorder_file_reservation_create(
            &g_audio_recorder_storage.reservation,
            g_audio_recorder_storage.temporary_path,
            AUDIO_RECORDER_WAV_HEADER_BYTES,
            AUDIO_RECORDER_INITIAL_RESERVE_BYTES);
    if ((created != RECORDER_FILE_RESERVATION_OK)
            && (created != RECORDER_FILE_RESERVATION_PARTIAL))
    {
        g_audio_recorder_storage.error =
            (created == RECORDER_FILE_RESERVATION_NO_SPACE)
                ? AUDIO_RECORDER_ERROR_NO_SPACE : AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_FAILED;
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
    config.reservation_critical_margin_us = AUDIO_RECORDER_RESERVATION_CRITICAL_US;
    config.estimated_write_us_per_sector = 250U;
    config.critical_enter = 0;
    config.critical_exit = 0;
    config.transport = generic_recorder_sd_block_device_adapter();
    config.reservation = generic_recorder_fatfs_reservation_adapter(
        &g_audio_recorder_storage.reservation);
    if (generic_recorder_begin(&g_audio_recorder_storage.recorder, &config) == 0U)
    {
        g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_FAILED;
        return 0U;
    }
    g_audio_recorder_capture.tail_cursor = 0U;
    g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_PREPARED;
    g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_NONE;
    return 1U;
}

static uint8_t audio_recorder_storage_cancel_physical(uint32_t session_id)
{
    if ((g_audio_recorder_storage.phase != AUDIO_RECORDER_STORAGE_PREPARED)
            || (g_audio_recorder_storage.prepare_session_id != session_id))
        return 0U;
    if (recorder_file_reservation_close(
            &g_audio_recorder_storage.reservation)
            != RECORDER_FILE_RESERVATION_OK)
        return 0U;
    if (sd_access_gate_try_acquire(
            SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
        return 0U;
    (void)f_unlink(g_audio_recorder_storage.temporary_path);
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
    audio_recorder_storage_release();
    return 1U;
}

uint8_t audio_recorder_storage_cancel(uint32_t session_id)
{
    if ((g_audio_recorder_storage.prepare_session_id != session_id)
            || ((g_audio_recorder_storage.prepare_pending == 0U)
                && (g_audio_recorder_storage.phase
                    != AUDIO_RECORDER_STORAGE_PREPARED)))
        return 0U;
    g_audio_recorder_storage.cancel_requested = 1U;
    g_audio_recorder_storage.prepare_pending = 0U;
    storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
    return 1U;
}

void audio_recorder_storage_release(void)
{
    generic_recorder_init(&g_audio_recorder_storage.recorder);
    recorder_file_reservation_init(&g_audio_recorder_storage.reservation);
    g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_IDLE;
    g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_storage.temporary_path[0] = '\0';
    g_audio_recorder_storage.final_path[0] = '\0';
    g_audio_recorder_storage.prepare_pending = 0U;
    g_audio_recorder_storage.cancel_requested = 0U;
}

void audio_recorder_storage_service(uint32_t session_id,
                                    uint8_t capture_is_active)
{
    audio_recorder_storage_runtime_t *const runtime =
        &g_audio_recorder_storage;
    if (runtime->cancel_requested != 0U
            && runtime->prepare_pending == 0U)
    {
        const uint32_t cancel_session = runtime->prepare_session_id;
        if (runtime->phase == AUDIO_RECORDER_STORAGE_PREPARED)
        {
            if (audio_recorder_storage_cancel_physical(cancel_session) == 0U)
            {
                storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
                return;
            }
        }
        runtime->cancel_requested = 0U;
        return;
    }
    if (runtime->prepare_pending != 0U)
    {
        char final_path[AUDIO_RECORDER_PATH_MAX];
        char temporary_path[AUDIO_RECORDER_PATH_MAX];
        uint8_t paths_ok = 0U;
        const audio_recorder_client_t client =
            (audio_recorder_client_t)runtime->prepare_client;
        if (client == AUDIO_RECORDER_CLIENT_LOOPER)
        {
            paths_ok = (looper_storage_make_next_path(
                runtime->prepare_looper_track, final_path,
                sizeof(final_path)) == LOOPER_STORAGE_PATH_OK) ? 1U : 0U;
            if ((paths_ok != 0U)
                    && (looper_storage_copy_wav_path_as_rec(
                        final_path, temporary_path,
                        sizeof(temporary_path)) == 0U))
                paths_ok = 0U;
        }
        else if (client == AUDIO_RECORDER_CLIENT_AUDIO_REC)
        {
            (void)snprintf(final_path, sizeof(final_path), "%s",
                           AUDIO_RECORDER_AUDIO_REC_WAV_PATH);
            (void)snprintf(temporary_path, sizeof(temporary_path), "%s",
                           AUDIO_RECORDER_AUDIO_REC_TEMP_PATH);
            paths_ok = 1U;
        }
        const uint32_t prepare_session = runtime->prepare_session_id;
        const uint32_t prepare_limit = runtime->prepare_frame_limit;
        runtime->prepare_pending = 0U;
        if ((paths_ok == 0U)
                || (audio_recorder_storage_prepare_physical(
                    temporary_path, final_path) == 0U))
        {
            if (runtime->phase != AUDIO_RECORDER_STORAGE_FAILED)
            {
                runtime->error = AUDIO_RECORDER_ERROR_SD_IO;
                runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
            }
        }
        else
        {
            runtime->prepare_session_id = prepare_session;
            (void)prepare_limit;
        }
        if (runtime->cancel_requested != 0U)
        {
            if (runtime->phase == AUDIO_RECORDER_STORAGE_PREPARED)
            {
                if (audio_recorder_storage_cancel_physical(prepare_session) != 0U)
                    runtime->cancel_requested = 0U;
                else
                    storage_io_owner_wakeup(STORAGE_OWNER_RECORDER);
            }
            else
                runtime->cancel_requested = 0U;
        }
    }
    if ((runtime->phase == AUDIO_RECORDER_STORAGE_IDLE)
            || (runtime->phase == AUDIO_RECORDER_STORAGE_TAKE_READY)
            || (runtime->phase == AUDIO_RECORDER_STORAGE_FAILED)) return;
    if (capture_is_active != 0U)
    {
        const uint32_t accepted_frames = g_audio_recorder_capture.head_cursor;
        __DMB();
        const uint64_t accepted_tail =
            (uint64_t)accepted_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
        runtime->recorder.accepted_frames = accepted_frames;
        runtime->recorder.accepted_tail = accepted_tail;
        runtime->recorder.metrics.frames_accepted = accepted_frames;
        runtime->recorder.metrics.bytes_accepted = accepted_tail;
        const uint32_t committed_frames = (uint32_t)(
            runtime->recorder.committed_tail
                / AUDIO_RECORDER_BYTES_PER_FRAME);
        __DMB();
        g_audio_recorder_capture.tail_cursor = committed_frames;
        const uint32_t retained = accepted_frames - committed_frames;
        if (retained > runtime->recorder.metrics.ring_high_watermark_frames)
            runtime->recorder.metrics.ring_high_watermark_frames = retained;
        const uint32_t free_frames =
            AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained;
        if (free_frames < runtime->recorder.metrics.ring_min_free_frames)
            runtime->recorder.metrics.ring_min_free_frames = free_frames;
        const uint64_t backlog = accepted_tail
            - runtime->recorder.committed_tail;
        if (backlog > runtime->recorder.metrics.max_backlog_bytes)
            runtime->recorder.metrics.max_backlog_bytes = backlog;

        if (g_audio_recorder_capture.capture_fault
                != AUDIO_RECORDER_ERROR_NONE)
        {
            runtime->error = (audio_recorder_error_t)
                g_audio_recorder_capture.capture_fault;
            runtime->recorder.error = GENERIC_RECORDER_ERROR_RING_FULL;
        }
        if ((g_audio_recorder_capture.closed_session == session_id)
                && (runtime->recorder.state == GENERIC_RECORDER_CAPTURING))
        {
            (void)generic_recorder_request_stop(
                &runtime->recorder, HAL_GetTick() * 1000U);
            runtime->phase = AUDIO_RECORDER_STORAGE_DRAINING;
        }
    }

    generic_recorder_service(&runtime->recorder, HAL_GetTick() * 1000U);
    if ((runtime->recorder.state == GENERIC_RECORDER_ERROR)
            || (runtime->recorder.state == GENERIC_RECORDER_ABORTED))
    {
        runtime->error = audio_recorder_storage_map_error(
            runtime->recorder.error);
        runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
    }
    else if ((runtime->recorder.state == GENERIC_RECORDER_DRAINING)
            && (runtime->phase != AUDIO_RECORDER_STORAGE_DRAINING))
    {
        runtime->error = audio_recorder_storage_map_error(
            runtime->recorder.error);
        runtime->phase = AUDIO_RECORDER_STORAGE_DRAINING;
    }
    else if ((runtime->recorder.state == GENERIC_RECORDER_FINALIZABLE)
            && (runtime->final_phase == AUDIO_RECORDER_FINAL_NONE))
    {
        runtime->phase = AUDIO_RECORDER_STORAGE_FINALIZING;
        runtime->final_phase = AUDIO_RECORDER_FINAL_COMMIT;
        runtime->final_started_ms = HAL_GetTick();
    }
    if (sd_scheduler_runtime_owner() == SD_SCHEDULER_OWNER_WRITE_DMA)
        runtime->metrics.storage_service_iterations_during_write++;
    sd_scheduler_runtime_service();
    generic_recorder_service(&runtime->recorder, HAL_GetTick() * 1000U);
}

audio_recorder_storage_phase_t audio_recorder_storage_phase(void)
{
    return g_audio_recorder_storage.phase;
}

audio_recorder_error_t audio_recorder_storage_error(void)
{
    return g_audio_recorder_storage.error;
}

void audio_recorder_storage_get_status(generic_recorder_status_t *status)
{
    if (status != 0) generic_recorder_get_status(
        &g_audio_recorder_storage.recorder, status);
}

void audio_recorder_storage_get_metrics(audio_recorder_metrics_t *metrics)
{
    if (metrics == 0) return;
    *metrics = g_audio_recorder_storage.metrics;
    metrics->recorder = g_audio_recorder_storage.recorder.metrics;
    metrics->reservation = g_audio_recorder_storage.reservation.metrics;
    sd_scheduler_runtime_metrics_get(&metrics->scheduler);
    sd_block_device_async_metrics_get(&metrics->block_device);
}

uint64_t audio_recorder_storage_committed_tail(void)
{
    return g_audio_recorder_storage.recorder.committed_tail;
}

uint8_t audio_recorder_storage_get_map_copy(
    audio_recorder_storage_map_copy_t *map)
{
    if (map == 0) return 0U;
    recorder_file_reservation_map_snapshot_t snapshot;
    if (recorder_file_reservation_map_snapshot(
            &g_audio_recorder_storage.reservation, &snapshot) == 0U)
        return 0U;
    if (snapshot.extent_count > RECORDER_FILE_RESERVATION_MAX_EXTENTS)
        return 0U;
    memset(map, 0, sizeof(*map));
    map->reserved_file_bytes = snapshot.reserved_file_bytes;
    map->valid_file_bytes = snapshot.valid_file_bytes;
    map->media_epoch = snapshot.media_epoch;
    map->extent_count = snapshot.extent_count;
    map->sector_size = snapshot.sector_size;
    if (map->extent_count != 0U)
        memcpy(map->extents, snapshot.extents,
               (size_t)map->extent_count * sizeof(map->extents[0]));
    return 1U;
}

uint8_t audio_recorder_storage_get_paths(const char **temporary_rec_path,
                                         const char **final_wav_path)
{
    if ((temporary_rec_path == 0) || (final_wav_path == 0)
            || (g_audio_recorder_storage.phase
                != AUDIO_RECORDER_STORAGE_PREPARED))
        return 0U;
    *temporary_rec_path = g_audio_recorder_storage.temporary_path;
    *final_wav_path = g_audio_recorder_storage.final_path;
    return 1U;
}
