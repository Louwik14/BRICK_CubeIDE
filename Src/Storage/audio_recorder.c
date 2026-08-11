#include "Storage/audio_recorder.h"

#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/generic_recorder_adapters.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include "stm32h7xx_hal.h"

#define AUDIO_RECORDER_RING_FRAMES (12001U)
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

SDRAM_RECORDER static audio_recorder_runtime_t g_audio_recorder;
SDRAM_RECORDER static int32_t
    g_audio_recorder_ring[AUDIO_RECORDER_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_audio_recorder_write_buffers[GENERIC_RECORDER_WRITE_BUFFER_COUNT]
                                  [AUDIO_RECORDER_WRITE_BUFFER_BYTES];

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

static void audio_recorder_critical_enter(void *context)
{
    (void)context;
    __disable_irq();
}

static void audio_recorder_critical_exit(void *context)
{
    (void)context;
    __enable_irq();
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
    g_audio_recorder.state = AUDIO_RECORDER_STATE_PREPARED;
    return 1U;
}

uint8_t audio_recorder_start_client(audio_recorder_client_t client)
{
    if ((g_audio_recorder.client != client)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED))
    {
        return 0U;
    }
    generic_recorder_config_t config;
    memset(&config, 0, sizeof(config));
    config.ring_interleaved = g_audio_recorder_ring;
    config.ring_capacity_frames = AUDIO_RECORDER_RING_FRAMES;
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
    config.critical_enter = audio_recorder_critical_enter;
    config.critical_exit = audio_recorder_critical_exit;
    config.transport = generic_recorder_sd_block_device_adapter();
    config.reservation = generic_recorder_fatfs_reservation_adapter(
        &g_audio_recorder.reservation);
    if (generic_recorder_begin(&g_audio_recorder.recorder, &config) == 0U)
    {
        g_audio_recorder.error = AUDIO_RECORDER_ERROR_SD_IO;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
        return 0U;
    }
    g_audio_recorder.state = AUDIO_RECORDER_STATE_RECORDING;
    return 1U;
}

uint8_t audio_recorder_push_from_irq_client(audio_recorder_client_t client,
                                            const int32_t *lr_interleaved,
                                            uint32_t frames)
{
    if ((g_audio_recorder.client != client)
            || (g_audio_recorder.state != AUDIO_RECORDER_STATE_RECORDING)
            || (lr_interleaved == 0) || (frames == 0U))
    {
        return 0U;
    }
    const uint64_t accepted_frames =
        g_audio_recorder.recorder.metrics.frames_accepted;
    if (accepted_frames >= g_audio_recorder.frame_limit)
    {
        return 1U;
    }
    const uint32_t remaining =
        g_audio_recorder.frame_limit - (uint32_t)accepted_frames;
    if (frames > remaining) frames = remaining;
    if (generic_recorder_push(
            &g_audio_recorder.recorder, lr_interleaved, frames) == 0U)
    {
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
        return 0U;
    }
    return 1U;
}

uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client)
{
    if (g_audio_recorder.client != client)
        return 0U;
    if ((g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY))
        return 1U;
    if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_RECORDING)
            || (generic_recorder_request_stop(
                    &g_audio_recorder.recorder,
                    HAL_GetTick() * 1000U) == 0U))
        return 0U;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_DRAINING;
    return 1U;
}

void audio_recorder_service(void)
{
    if (sd_scheduler_runtime_owner() == SD_SCHEDULER_OWNER_WRITE_DMA)
        g_audio_recorder.metrics.superloop_iterations_during_write++;
    generic_recorder_service(
        &g_audio_recorder.recorder, HAL_GetTick() * 1000U);
    if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_ERROR)
            || (g_audio_recorder.recorder.state == GENERIC_RECORDER_ABORTED))
    {
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
    }
    else if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_DRAINING)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING))
    {
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
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
    status->frames_received = (uint32_t)(
        generic_status.accepted_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
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
    return ((g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FINALIZING))
        ? 1U : 0U;
}

uint8_t audio_recorder_get_live_stream(audio_recorder_client_t client,
                                       audio_recorder_live_stream_t *stream)
{
    if ((stream == 0) || (g_audio_recorder.client != client)
            || (g_audio_recorder.state < AUDIO_RECORDER_STATE_DRAINING)
            || (g_audio_recorder.state == AUDIO_RECORDER_STATE_FAILED)
            || (recorder_file_reservation_map_snapshot(
                    &g_audio_recorder.reservation, &stream->reservation) == 0U))
    {
        return 0U;
    }
    generic_recorder_status_t status;
    generic_recorder_get_status(&g_audio_recorder.recorder, &status);
    stream->path = (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY)
        ? g_audio_recorder.final_path : g_audio_recorder.temporary_path;
    stream->accepted_frames = (uint32_t)(status.accepted_tail
                                         / AUDIO_RECORDER_BYTES_PER_FRAME);
    stream->committed_frames = (uint32_t)(status.committed_tail
                                          / AUDIO_RECORDER_BYTES_PER_FRAME);
    return 1U;
}

uint8_t audio_recorder_client_is_active(audio_recorder_client_t client)
{
    return ((g_audio_recorder.client == client)
            && (audio_recorder_is_active() != 0U)) ? 1U : 0U;
}

uint8_t audio_recorder_prepare(const char *temporary_rec_path,
                               const char *final_wav_path,
                               uint32_t frame_limit)
{
    return audio_recorder_prepare_client(AUDIO_RECORDER_CLIENT_AUDIO_REC,
                                         temporary_rec_path,
                                         final_wav_path,
                                         frame_limit);
}

uint8_t audio_recorder_start(void)
{
    return audio_recorder_start_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
}

uint8_t audio_recorder_push_from_irq(const int32_t *lr_interleaved,
                                     uint32_t frames)
{
    return audio_recorder_push_from_irq_client(
        AUDIO_RECORDER_CLIENT_AUDIO_REC, lr_interleaved, frames);
}

uint8_t audio_recorder_request_stop(void)
{
    return audio_recorder_request_stop_client(AUDIO_RECORDER_CLIENT_AUDIO_REC);
}

uint8_t audio_recorder_get_status(audio_recorder_status_t *status)
{
    return audio_recorder_get_status_client(
        AUDIO_RECORDER_CLIENT_AUDIO_REC, status);
}

uint8_t audio_recorder_get_last_take(const char **path, uint32_t *frames)
{
    return audio_recorder_get_last_take_client(
        AUDIO_RECORDER_CLIENT_AUDIO_REC, path, frames);
}
