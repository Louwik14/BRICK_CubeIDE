#include "Storage/audio_recorder_storage.h"

#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/generic_recorder_adapters.h"
#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "IPC/audio_recorder_capture_contract.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "main.h"

#define AUDIO_RECORDER_WRITE_BUFFER_BYTES (32768U)
#define AUDIO_RECORDER_MINIMUM_WRITE_BYTES (8192U)
#define AUDIO_RECORDER_INITIAL_RESERVE_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_EXTENSION_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_RESERVATION_LOW_US (3000000U)
#define AUDIO_RECORDER_RESERVATION_CRITICAL_US (1000000U)

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

typedef enum
{
    AUDIO_RECORDER_PREP_NONE = 0,
    AUDIO_RECORDER_PREP_REMOVE_TEMPORARY,
    AUDIO_RECORDER_PREP_CREATE,
    AUDIO_RECORDER_PREP_RESERVE,
    AUDIO_RECORDER_PREP_DONE
} audio_recorder_prepare_phase_t;

typedef struct
{
    generic_recorder_t recorder;
    recorder_file_reservation_t reservation;
    sd_scheduler_provider_t recorder_filesystem_provider;
    audio_recorder_storage_phase_t phase;
    audio_recorder_error_t error;
    audio_recorder_final_phase_t final_phase;
    audio_recorder_prepare_phase_t prepare_phase;
    uint32_t filesystem_generation;
    uint32_t filesystem_media_epoch;
    uint32_t filesystem_io_lba;
    uint8_t filesystem_io_active;
    ALIGN32 uint8_t wav_header[AUDIO_RECORDER_WAV_HEADER_BYTES];
    char temporary_path[AUDIO_RECORDER_PATH_MAX];
    char final_path[AUDIO_RECORDER_PATH_MAX];
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
    if ((runtime->phase != AUDIO_RECORDER_STORAGE_PREPARING)
            && (runtime->recorder_filesystem_provider.peek != 0)
            && (runtime->recorder_filesystem_provider.peek(
                    runtime->recorder_filesystem_provider.context,
                    candidate) != 0U))
        return 1U;
    if ((runtime->phase != AUDIO_RECORDER_STORAGE_PREPARING)
            && ((runtime->phase != AUDIO_RECORDER_STORAGE_FINALIZING)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_NONE)
            || (runtime->final_phase == AUDIO_RECORDER_FINAL_DONE)))
        return 0U;
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_FILESYSTEM;
    candidate->ready = 1U;
    candidate->margin_us = SD_SCHEDULER_MARGIN_UNKNOWN;
    candidate->estimated_cost_us = 100000U;
    candidate->owner_generation = (runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
        ? runtime->filesystem_generation : runtime->recorder.generation;
    candidate->media_epoch = (runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
        ? runtime->filesystem_media_epoch : runtime->recorder.media_epoch;
    candidate->reservation = SD_SCHEDULER_RESERVATION_SAFE;
    return 1U;
}

static uint8_t audio_recorder_storage_start_writer(
    audio_recorder_storage_runtime_t *runtime)
{
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
    config.transport = generic_recorder_sd_block_device_adapter();
    config.reservation = generic_recorder_fatfs_reservation_adapter(&runtime->reservation);
    return generic_recorder_begin(&runtime->recorder, &config);
}

static sd_scheduler_start_result_t audio_recorder_storage_preparation_step(
    audio_recorder_storage_runtime_t *runtime)
{
    recorder_file_reservation_result_t rr;
    switch (runtime->prepare_phase)
    {
        case AUDIO_RECORDER_PREP_REMOVE_TEMPORARY:
        {
            const FRESULT fr = f_unlink(runtime->temporary_path);
            if ((fr != FR_OK) && (fr != FR_NO_FILE)) return SD_SCHEDULER_START_ERROR;
            runtime->prepare_phase = AUDIO_RECORDER_PREP_CREATE;
            return SD_SCHEDULER_START_COMPLETED;
        }
        case AUDIO_RECORDER_PREP_CREATE:
            rr = recorder_file_reservation_create(&runtime->reservation,
                runtime->temporary_path, AUDIO_RECORDER_WAV_HEADER_BYTES, 0U);
            if (rr == RECORDER_FILE_RESERVATION_SD_BUSY) return SD_SCHEDULER_START_BUSY;
            if ((rr != RECORDER_FILE_RESERVATION_OK)
                    && (rr != RECORDER_FILE_RESERVATION_PARTIAL))
                return SD_SCHEDULER_START_ERROR;
            rr = recorder_file_reservation_extend_begin(&runtime->reservation,
                (AUDIO_RECORDER_INITIAL_RESERVE_BYTES + AUDIO_RECORDER_WAV_HEADER_BYTES)
                    - runtime->reservation.fs_state.reserved_bytes);
            if (rr != RECORDER_FILE_RESERVATION_OK) return SD_SCHEDULER_START_ERROR;
            runtime->prepare_phase = AUDIO_RECORDER_PREP_RESERVE;
            return SD_SCHEDULER_START_COMPLETED;
        case AUDIO_RECORDER_PREP_RESERVE:
            rr = recorder_file_reservation_job_step(&runtime->reservation);
            if (rr == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_START_STARTED;
            if (rr == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_START_COMPLETED;
            if (rr == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (rr != RECORDER_FILE_RESERVATION_OK) return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_job_finish(&runtime->reservation);
            if (audio_recorder_storage_start_writer(runtime) == 0U)
                return SD_SCHEDULER_START_ERROR;
            g_audio_recorder_capture.tail_cursor = 0U;
            runtime->prepare_phase = AUDIO_RECORDER_PREP_DONE;
            runtime->phase = AUDIO_RECORDER_STORAGE_PREPARED;
            sd_access_gate_set_recorder_fs_logical_active(0U);
            return SD_SCHEDULER_START_COMPLETED;
        default:
            return SD_SCHEDULER_START_ERROR;
    }
}

static sd_scheduler_start_result_t audio_recorder_storage_finalization_step(
    audio_recorder_storage_runtime_t *runtime)
{
    recorder_file_reservation_result_t reservation_result;
    switch (runtime->final_phase)
    {
        case AUDIO_RECORDER_FINAL_COMMIT:
            if (recorder_file_reservation_job_active(&runtime->reservation) == 0U)
            {
                reservation_result = recorder_file_reservation_commit_begin(
                    &runtime->reservation, runtime->recorder.committed_tail);
                if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                    return SD_SCHEDULER_START_BUSY;
                return (reservation_result == RECORDER_FILE_RESERVATION_OK)
                    ? SD_SCHEDULER_START_COMPLETED : SD_SCHEDULER_START_ERROR;
            }
            reservation_result = recorder_file_reservation_job_step(
                &runtime->reservation);
            if (reservation_result == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_START_STARTED;
            if (reservation_result == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_START_COMPLETED;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_job_finish(&runtime->reservation);
            sd_access_gate_set_recorder_fs_logical_active(1U);
            runtime->final_phase = AUDIO_RECORDER_FINAL_RELEASE;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_RELEASE:
            if (recorder_file_reservation_job_active(&runtime->reservation) == 0U)
            {
                reservation_result = recorder_file_reservation_release_begin(
                    &runtime->reservation);
                if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                    return SD_SCHEDULER_START_BUSY;
                return (reservation_result == RECORDER_FILE_RESERVATION_OK)
                    ? SD_SCHEDULER_START_COMPLETED : SD_SCHEDULER_START_ERROR;
            }
            reservation_result = recorder_file_reservation_job_step(
                &runtime->reservation);
            if (reservation_result == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_START_STARTED;
            if (reservation_result == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_START_COMPLETED;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_job_finish(&runtime->reservation);
            sd_access_gate_set_recorder_fs_logical_active(1U);
            runtime->final_phase = AUDIO_RECORDER_FINAL_HEADER;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_HEADER:
            if ((runtime->recorder.committed_tail > UINT32_MAX)
                    || (audio_recorder_wav_build_header(
                        runtime->wav_header, (uint32_t)runtime->recorder.committed_tail,
                        AUDIO_RECORDER_SAMPLE_RATE_HZ,
                        AUDIO_RECORDER_CHANNELS) == 0U))
                return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_map_snapshot_t map;
            sample_stream_physical_span_t span;
            if ((recorder_file_reservation_map_snapshot(&runtime->reservation, &map) == 0U)
                    || (recorder_file_reservation_map_resolve(&map, 0U,
                        AUDIO_RECORDER_WAV_HEADER_BYTES, &span) == 0U)
                    || (span.first_sector_skip != 0U)
                    || (span.logical_bytes < AUDIO_RECORDER_WAV_HEADER_BYTES))
                return SD_SCHEDULER_START_ERROR;
            runtime->filesystem_io_lba = span.lba;
            if (sd_block_device_async_write_submit(span.lba, 1U,
                    runtime->wav_header, runtime->recorder.generation)
                    != SD_BLOCK_DEVICE_OK)
                return SD_SCHEDULER_START_BUSY;
            runtime->filesystem_io_active = 1U;
            return SD_SCHEDULER_START_STARTED;

        case AUDIO_RECORDER_FINAL_SYNC:
            if (recorder_file_reservation_job_active(&runtime->reservation) == 0U)
            {
                reservation_result = recorder_file_reservation_sync_begin(
                    &runtime->reservation);
                if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                    return SD_SCHEDULER_START_BUSY;
                return (reservation_result == RECORDER_FILE_RESERVATION_OK)
                    ? SD_SCHEDULER_START_COMPLETED : SD_SCHEDULER_START_ERROR;
            }
            reservation_result = recorder_file_reservation_job_step(
                &runtime->reservation);
            if (reservation_result == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_START_STARTED;
            if (reservation_result == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_START_COMPLETED;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_job_finish(&runtime->reservation);
            sd_access_gate_set_recorder_fs_logical_active(1U);
            runtime->final_phase = AUDIO_RECORDER_FINAL_CLOSE;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_CLOSE:
            reservation_result = recorder_file_reservation_close(
                &runtime->reservation);
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            runtime->final_phase = AUDIO_RECORDER_FINAL_RENAME;
            return SD_SCHEDULER_START_COMPLETED;

        case AUDIO_RECORDER_FINAL_RENAME:
            if (recorder_file_reservation_job_active(&runtime->reservation) == 0U)
            {
                reservation_result = recorder_file_reservation_rename_begin(
                    &runtime->reservation, runtime->final_path);
                if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                    return SD_SCHEDULER_START_BUSY;
                return (reservation_result == RECORDER_FILE_RESERVATION_OK)
                    ? SD_SCHEDULER_START_COMPLETED : SD_SCHEDULER_START_ERROR;
            }
            reservation_result = recorder_file_reservation_job_step(
                &runtime->reservation);
            if (reservation_result == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_START_STARTED;
            if (reservation_result == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_START_COMPLETED;
            if (reservation_result == RECORDER_FILE_RESERVATION_SD_BUSY)
                return SD_SCHEDULER_START_BUSY;
            if (reservation_result != RECORDER_FILE_RESERVATION_OK)
                return SD_SCHEDULER_START_ERROR;
            recorder_file_reservation_job_finish(&runtime->reservation);
            runtime->final_phase = AUDIO_RECORDER_FINAL_DONE;
            runtime->phase = AUDIO_RECORDER_STORAGE_TAKE_READY;
            sd_access_gate_set_recorder_fs_logical_active(0U);
            return SD_SCHEDULER_START_COMPLETED;

        default:
            return SD_SCHEDULER_START_ERROR;
    }
}

static sd_scheduler_poll_result_t audio_recorder_storage_filesystem_poll(
    void *context)
{
    audio_recorder_storage_runtime_t *const runtime = context;
    if (runtime == 0)
        return SD_SCHEDULER_POLL_ERROR;
    if (runtime->reservation.job_io_active != 0U)
    {
        if ((runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
                || (runtime->reservation.job_phase == RECORDER_FILE_JOB_COMMIT)
                || (runtime->reservation.job_phase == RECORDER_FILE_JOB_RELEASE)
                || (runtime->reservation.job_phase == RECORDER_FILE_JOB_SYNC)
                || (runtime->reservation.job_phase == RECORDER_FILE_JOB_RENAME))
        {
            const recorder_file_reservation_result_t result =
                recorder_file_reservation_job_poll(&runtime->reservation);
            if (result == RECORDER_FILE_RESERVATION_IO_STARTED)
                return SD_SCHEDULER_POLL_ACTIVE;
            if (result == RECORDER_FILE_RESERVATION_RECOVERY_ABORT)
                return SD_SCHEDULER_POLL_RECOVERY_ABORT;
            if (result == RECORDER_FILE_RESERVATION_PROGRESS)
                return SD_SCHEDULER_POLL_COMPLETED;
            recorder_file_reservation_job_finish(&runtime->reservation);
            runtime->error = AUDIO_RECORDER_ERROR_SD_IO;
            runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
            sd_access_gate_set_recorder_fs_logical_active(0U);
            return SD_SCHEDULER_POLL_ERROR;
        }
        if (runtime->recorder_filesystem_provider.poll != 0)
        {
            return runtime->recorder_filesystem_provider.poll(
                runtime->recorder_filesystem_provider.context);
        }
        return SD_SCHEDULER_POLL_ERROR;
    }
    if (runtime->filesystem_io_active == 0U)
        return SD_SCHEDULER_POLL_ERROR;
    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    if (sd_block_device_async_take_completion(&completion) == 0U)
    {
        return (sd_block_device_async_hardware_state() == SD_BLOCK_DEVICE_HW_ABORTING)
            ? SD_SCHEDULER_POLL_RECOVERY_ABORT : SD_SCHEDULER_POLL_ACTIVE;
    }
    runtime->filesystem_io_active = 0U;
    if ((completion.result != SD_BLOCK_DEVICE_OK)
            || (completion.operation != SD_BLOCK_DEVICE_OPERATION_WRITE)
            || (completion.lba != runtime->filesystem_io_lba)
            || (completion.owner_generation != runtime->recorder.generation)
            || (completion.media_epoch != runtime->recorder.media_epoch))
    {
        runtime->error = (completion.result == SD_BLOCK_DEVICE_MEDIA_CHANGED)
            ? AUDIO_RECORDER_ERROR_MEDIA_CHANGED : AUDIO_RECORDER_ERROR_SD_IO;
        runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
        sd_access_gate_set_recorder_fs_logical_active(0U);
        return SD_SCHEDULER_POLL_ERROR;
    }
    runtime->final_phase = AUDIO_RECORDER_FINAL_SYNC;
    return SD_SCHEDULER_POLL_COMPLETED;
}

static sd_scheduler_start_result_t audio_recorder_storage_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    audio_recorder_storage_runtime_t *const runtime = context;
    if ((runtime == 0) || (candidate == 0)
            || (candidate->owner_generation != ((runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
                ? runtime->filesystem_generation : runtime->recorder.generation)))
        return SD_SCHEDULER_START_ERROR;
    if (runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
    {
        const sd_scheduler_start_result_t prep =
            audio_recorder_storage_preparation_step(runtime);
        if (prep == SD_SCHEDULER_START_ERROR)
        {
            recorder_file_reservation_job_finish(&runtime->reservation);
            runtime->error = AUDIO_RECORDER_ERROR_SD_IO;
            runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
            sd_access_gate_set_recorder_fs_logical_active(0U);
        }
        return prep;
    }
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
        .poll = audio_recorder_storage_filesystem_poll,
    };
    (void)sd_scheduler_runtime_bind_recorder(
        &write_provider, &filesystem_provider);
}

audio_recorder_lifecycle_result_t audio_recorder_storage_prepare(
    const char *temporary_rec_path,
    const char *final_wav_path)
{
    if ((temporary_rec_path == 0) || (final_wav_path == 0))
        return AUDIO_RECORDER_LIFECYCLE_ERROR;
    if ((strlen(temporary_rec_path) >= AUDIO_RECORDER_PATH_MAX)
            || (strlen(final_wav_path) >= AUDIO_RECORDER_PATH_MAX))
        return AUDIO_RECORDER_LIFECYCLE_ERROR;
    if (g_audio_recorder_storage.phase == AUDIO_RECORDER_STORAGE_PREPARING)
    {
        return ((strcmp(g_audio_recorder_storage.temporary_path, temporary_rec_path) == 0)
                && (strcmp(g_audio_recorder_storage.final_path, final_wav_path) == 0))
            ? AUDIO_RECORDER_LIFECYCLE_NOT_NOW : AUDIO_RECORDER_LIFECYCLE_ERROR;
    }
    if (g_audio_recorder_storage.phase == AUDIO_RECORDER_STORAGE_PREPARED)
    {
        return ((strcmp(g_audio_recorder_storage.temporary_path, temporary_rec_path) == 0)
                && (strcmp(g_audio_recorder_storage.final_path, final_wav_path) == 0))
            ? AUDIO_RECORDER_LIFECYCLE_OK : AUDIO_RECORDER_LIFECYCLE_ERROR;
    }
    if (g_audio_recorder_storage.phase != AUDIO_RECORDER_STORAGE_IDLE)
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    (void)strcpy(g_audio_recorder_storage.temporary_path, temporary_rec_path);
    (void)strcpy(g_audio_recorder_storage.final_path, final_wav_path);
    generic_recorder_init(&g_audio_recorder_storage.recorder);
    recorder_file_reservation_init(&g_audio_recorder_storage.reservation);
    g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_storage.final_phase = AUDIO_RECORDER_FINAL_NONE;

    g_audio_recorder_storage.filesystem_generation++;
    if (g_audio_recorder_storage.filesystem_generation == 0U)
        g_audio_recorder_storage.filesystem_generation = 1U;
    g_audio_recorder_storage.filesystem_media_epoch = sd_access_media_epoch();
    g_audio_recorder_storage.prepare_phase = AUDIO_RECORDER_PREP_REMOVE_TEMPORARY;
    g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_PREPARING;
    sd_access_gate_set_recorder_fs_logical_active(1U);
    return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
}

audio_recorder_lifecycle_result_t audio_recorder_storage_cancel(void)
{
    if (g_audio_recorder_storage.phase == AUDIO_RECORDER_STORAGE_IDLE)
        return AUDIO_RECORDER_LIFECYCLE_OK;

    generic_recorder_abort(&g_audio_recorder_storage.recorder);
    if (g_audio_recorder_storage.reservation.open != 0U)
    {
        const recorder_file_reservation_result_t closed =
            recorder_file_reservation_close(
                &g_audio_recorder_storage.reservation);
        if (closed == RECORDER_FILE_RESERVATION_SD_BUSY)
            return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
        if (closed != RECORDER_FILE_RESERVATION_OK)
        {
            g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_SD_IO;
            g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_FAILED;
            return AUDIO_RECORDER_LIFECYCLE_ERROR;
        }
    }
    if (sd_access_gate_try_acquire(
            SD_ACCESS_CLIENT_SCHEDULED_RECORDER) == 0U)
        return AUDIO_RECORDER_LIFECYCLE_NOT_NOW;
    const FRESULT temporary_result =
        (g_audio_recorder_storage.temporary_path[0] != '\0')
            ? f_unlink(g_audio_recorder_storage.temporary_path) : FR_NO_FILE;
    const FRESULT final_result =
        (g_audio_recorder_storage.final_path[0] != '\0')
            ? f_unlink(g_audio_recorder_storage.final_path) : FR_NO_FILE;
    sd_access_gate_release(SD_ACCESS_CLIENT_SCHEDULED_RECORDER);
    audio_recorder_storage_release();
    return (((temporary_result == FR_OK) || (temporary_result == FR_NO_FILE))
            && ((final_result == FR_OK) || (final_result == FR_NO_FILE)))
        ? AUDIO_RECORDER_LIFECYCLE_OK : AUDIO_RECORDER_LIFECYCLE_ERROR;
}

void audio_recorder_storage_release(void)
{
    generic_recorder_init(&g_audio_recorder_storage.recorder);
    recorder_file_reservation_init(&g_audio_recorder_storage.reservation);
    g_audio_recorder_storage.phase = AUDIO_RECORDER_STORAGE_IDLE;
    g_audio_recorder_storage.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_storage.temporary_path[0] = '\0';
    g_audio_recorder_storage.final_path[0] = '\0';
    sd_access_gate_set_recorder_fs_logical_active(0U);
}

void audio_recorder_storage_service(uint32_t session_id,
                                    uint8_t capture_is_active)
{
    audio_recorder_storage_runtime_t *const runtime =
        &g_audio_recorder_storage;
    if ((runtime->phase == AUDIO_RECORDER_STORAGE_IDLE)
            || (runtime->phase == AUDIO_RECORDER_STORAGE_TAKE_READY)
            || (runtime->phase == AUDIO_RECORDER_STORAGE_FAILED)) return;
    const uint32_t current_epoch = sd_access_media_epoch();
    if (((runtime->phase == AUDIO_RECORDER_STORAGE_PREPARING)
            && (runtime->filesystem_media_epoch != current_epoch))
            || ((runtime->phase != AUDIO_RECORDER_STORAGE_PREPARING)
                && (runtime->recorder.media_epoch != 0U)
                && (runtime->recorder.media_epoch != current_epoch)))
    {
        runtime->error = AUDIO_RECORDER_ERROR_MEDIA_CHANGED;
        runtime->recorder.error = GENERIC_RECORDER_ERROR_MEDIA_CHANGED;
        runtime->recorder.state = GENERIC_RECORDER_ERROR;
        runtime->phase = AUDIO_RECORDER_STORAGE_FAILED;
        sd_access_gate_set_recorder_fs_logical_active(0U);
        return;
    }
    if (capture_is_active != 0U)
    {
        const uint32_t accepted_frames = g_audio_recorder_capture.head_cursor;
        __DMB();
        const uint64_t accepted_tail =
            (uint64_t)accepted_frames * AUDIO_RECORDER_BYTES_PER_FRAME;
        runtime->recorder.accepted_frames = accepted_frames;
        runtime->recorder.accepted_tail = accepted_tail;
        const uint32_t committed_frames = (uint32_t)(
            runtime->recorder.committed_tail
                / AUDIO_RECORDER_BYTES_PER_FRAME);
        __DMB();
        g_audio_recorder_capture.tail_cursor = committed_frames;
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
            (void)generic_recorder_request_stop(&runtime->recorder);
            runtime->phase = AUDIO_RECORDER_STORAGE_DRAINING;
        }
    }

    generic_recorder_service(&runtime->recorder);
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
        sd_access_gate_set_recorder_fs_logical_active(1U);
    }
    sd_scheduler_runtime_service();
    generic_recorder_service(&runtime->recorder);
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
