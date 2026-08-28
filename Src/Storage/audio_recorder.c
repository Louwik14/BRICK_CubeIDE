#include "Storage/audio_recorder.h"

#include <string.h>

#include "SD/sd_scheduler_runtime.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/generic_recorder_adapters.h"
#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Audio/control_audio_command.h"
#include "Core/control_audio_publication.h"
#include "Core/intercore_cache.h"
#include "Core/live_clock.h"
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

typedef struct
{
    volatile uint32_t generation;
    volatile uint32_t accepted_frames;
    volatile uint32_t released_frames;
    volatile uint32_t frame_limit;
    volatile uint32_t stop_generation;
    volatile uint32_t error;
    volatile uint8_t client;
    volatile uint8_t prepared;
    volatile uint8_t active;
    uint8_t reserved;
} audio_recorder_capture_transport_t;

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t accepted_frames;
    volatile uint32_t committed_frames;
    volatile uint8_t client;
    volatile uint8_t valid;
    uint8_t reserved[2];
    volatile char path[AUDIO_RECORDER_PATH_MAX];
    recorder_file_reservation_map_owned_t reservation;
} audio_recorder_live_publication_t;

_Static_assert(sizeof(audio_recorder_live_publication_t) == 1680U,
               "Recorder live publication ABI changed");

_Static_assert(sizeof(audio_recorder_capture_transport_t) == 28U,
               "Recorder capture transport ABI changed");

/* FatFs, callbacks and generic-recorder pointers are strictly M4-private. */
STORAGE_STATE_SDRAM static audio_recorder_runtime_t g_audio_recorder;
SDRAM_RECORDER static int32_t
    g_audio_recorder_ring[AUDIO_RECORDER_RING_FRAMES * AUDIO_RECORDER_CHANNELS];
D3_IPC static audio_recorder_capture_transport_t g_audio_recorder_capture;
AUDIO_STORAGE_SHARED_SDRAM static audio_recorder_live_publication_t
    g_audio_recorder_live_publication;
RECORDER_SCRATCH_SDRAM static uint8_t
    g_audio_recorder_write_buffers[GENERIC_RECORDER_WRITE_BUFFER_COUNT]
                                  [AUDIO_RECORDER_WRITE_BUFFER_BYTES];

typedef struct
{
    uint32_t generation;
    uint32_t frame_limit;
    uint16_t id;
    uint8_t client;
    uint8_t reserved;
} audio_recorder_prepared_config_t;

D2_IPC static audio_recorder_prepared_config_t g_audio_recorder_config[8];
static uint32_t g_audio_recorder_control_generation;
static uint16_t g_audio_recorder_control_config_id;
static uint16_t g_audio_recorder_next_config_id;

typedef struct
{
    uint64_t actual_start_sample;
    uint64_t target_stop_sample;
    uint32_t expected_frames;
    uint8_t track;
    uint8_t start_armed;
    uint8_t stop_armed;
    uint8_t recording;
} audio_recorder_looper_control_t;

static audio_recorder_looper_control_t g_audio_recorder_looper_control;

static uint8_t audio_recorder_publish_start_client_at(
    audio_recorder_client_t client, uint64_t sample_time)
{
    if ((g_audio_recorder.state != AUDIO_RECORDER_STATE_PREPARED)
            || (g_audio_recorder.client != client)
            || (g_audio_recorder_control_config_id == 0U))
        return 0U;
    const uint8_t published = control_audio_publish_record(CONTROL_AUDIO_RECORD_START,
        g_audio_recorder_control_generation, g_audio_recorder_control_config_id,
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
        g_audio_recorder_control_generation, 0U, (uint8_t)client,
        sample_time);
}

static uint16_t audio_recorder_prepare_audio_config(uint8_t client,
                                                    uint32_t frame_limit,
                                                    uint32_t generation)
{
    for (uint8_t i = 0U; i < 8U; ++i)
        if (g_audio_recorder_config[i].id == 0U)
        {
            uint16_t id = g_audio_recorder_next_config_id++;
            if ((id == 0U) || ((id & 0x8000U) != 0U))
            { g_audio_recorder_next_config_id = 2U; id = 1U; }
            g_audio_recorder_config[i] = (audio_recorder_prepared_config_t){
                .generation = generation, .frame_limit = frame_limit,
                .id = id, .client = client };
            __DMB();
            return id;
        }
    return 0U;
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

static void audio_recorder_publish_live_stream(void)
{
    recorder_file_reservation_map_owned_t reservation;
    const uint8_t reservation_valid =
        recorder_file_reservation_map_snapshot_owned(
            &g_audio_recorder.reservation, &reservation);
    uint32_t sequence = g_audio_recorder_live_publication.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    g_audio_recorder_live_publication.sequence = sequence + 1U;
    intercore_cache_publish((const void *)&g_audio_recorder_live_publication.sequence,
                            sizeof(g_audio_recorder_live_publication.sequence));
    generic_recorder_status_t status;
    generic_recorder_get_status(&g_audio_recorder.recorder, &status);
    g_audio_recorder_live_publication.accepted_frames =
        g_audio_recorder_capture.accepted_frames;
    g_audio_recorder_live_publication.committed_frames = (uint32_t)(
        status.committed_tail / AUDIO_RECORDER_BYTES_PER_FRAME);
    g_audio_recorder_live_publication.client = (uint8_t)g_audio_recorder.client;
    g_audio_recorder_live_publication.valid = (uint8_t)(
        (g_audio_recorder.client != AUDIO_RECORDER_CLIENT_NONE)
        && (g_audio_recorder.state >= AUDIO_RECORDER_STATE_DRAINING)
        && (g_audio_recorder.state != AUDIO_RECORDER_STATE_FAILED)
        && (reservation_valid != 0U));
    const char *const path = (g_audio_recorder.state == AUDIO_RECORDER_STATE_TAKE_READY)
        ? g_audio_recorder.final_path : g_audio_recorder.temporary_path;
    for (uint32_t i = 0U; i < AUDIO_RECORDER_PATH_MAX; ++i)
    {
        g_audio_recorder_live_publication.path[i] = path[i];
        if (path[i] == '\0') break;
    }
    if (reservation_valid != 0U)
    {
        g_audio_recorder_live_publication.reservation = reservation;
    }
    __DMB();
    g_audio_recorder_live_publication.sequence = sequence + 2U;
    if (g_audio_recorder_live_publication.sequence == 0U)
        g_audio_recorder_live_publication.sequence = 2U;
    intercore_cache_publish(&g_audio_recorder_live_publication,
                            sizeof(g_audio_recorder_live_publication));
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
    memset(&g_audio_recorder_capture, 0, sizeof(g_audio_recorder_capture));
    memset(g_audio_recorder_config, 0, sizeof(g_audio_recorder_config));
    g_audio_recorder_control_generation = 0U;
    g_audio_recorder_control_config_id = 0U;
    g_audio_recorder_next_config_id = 1U;
    memset(&g_audio_recorder_looper_control, 0,
           sizeof(g_audio_recorder_looper_control));
    g_audio_recorder_looper_control.track = 0xFFU;
    memset(&g_audio_recorder_live_publication, 0,
           sizeof(g_audio_recorder_live_publication));
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
    uint32_t generation = g_audio_recorder_control_generation + 1U;
    if (generation == 0U) generation = 1U;
    const uint16_t config_id = audio_recorder_prepare_audio_config(
        (uint8_t)client, g_audio_recorder.frame_limit, generation);
    if (config_id == 0U) return 0U;
    g_audio_recorder_control_generation = generation;
    g_audio_recorder_control_config_id = config_id;
    g_audio_recorder.state = AUDIO_RECORDER_STATE_PREPARED;
    return 1U;
}

uint8_t audio_recorder_start_client(audio_recorder_client_t client)
{
    uint64_t sample_time = 0U;
    if (!live_clock_read_audio_sample(&sample_time)) return 0U;
    return audio_recorder_publish_start_client_at(client, sample_time);
}

uint8_t audio_recorder_push_from_irq_client(audio_recorder_client_t client,
                                            const int32_t *lr_interleaved,
                                            uint32_t frames)
{
    if ((g_audio_recorder_capture.client != (uint8_t)client)
            || (g_audio_recorder_capture.prepared == 0U)
            || (g_audio_recorder_capture.active == 0U)
            || (lr_interleaved == 0) || (frames == 0U))
    {
        return 0U;
    }
    const uint32_t accepted_frames = g_audio_recorder_capture.accepted_frames;
    if (accepted_frames >= g_audio_recorder_capture.frame_limit)
    {
        return 1U;
    }
    const uint32_t remaining =
        g_audio_recorder_capture.frame_limit - accepted_frames;
    if (frames > remaining) frames = remaining;
    const uint32_t released_frames = g_audio_recorder_capture.released_frames;
    __DMB();
    const uint32_t retained = accepted_frames - released_frames;
    if (frames > (AUDIO_RECORDER_RING_FRAMES - retained))
    {
        g_audio_recorder_capture.error = AUDIO_RECORDER_ERROR_RING_OVERFLOW;
        g_audio_recorder_capture.active = 0U;
        __DMB();
        g_audio_recorder_capture.stop_generation =
            g_audio_recorder_capture.generation;
        return 0U;
    }
    const uint32_t write_index = accepted_frames % AUDIO_RECORDER_RING_FRAMES;
    uint32_t first = AUDIO_RECORDER_RING_FRAMES - write_index;
    if (first > frames) first = frames;
    memcpy(&g_audio_recorder_ring[write_index * AUDIO_RECORDER_CHANNELS],
           lr_interleaved,
           (size_t)first * AUDIO_RECORDER_CHANNELS * sizeof(int32_t));
    if (frames > first)
    {
        memcpy(g_audio_recorder_ring,
               &lr_interleaved[first * AUDIO_RECORDER_CHANNELS],
               (size_t)(frames - first) * AUDIO_RECORDER_CHANNELS
                   * sizeof(int32_t));
    }
    __DMB();
    g_audio_recorder_capture.accepted_frames = accepted_frames + frames;
    if (g_audio_recorder_capture.accepted_frames
            >= g_audio_recorder_capture.frame_limit)
    {
        g_audio_recorder_capture.active = 0U;
        __DMB();
        g_audio_recorder_capture.stop_generation =
            g_audio_recorder_capture.generation;
    }
    return 1U;
}

uint8_t audio_recorder_request_stop_client(audio_recorder_client_t client)
{
    uint64_t sample_time = 0U;
    if (!live_clock_read_audio_sample(&sample_time)) return 0U;
    return audio_recorder_publish_stop_client_at(client, sample_time);
}

uint8_t audio_recorder_control_arm_looper(uint8_t track,
                                          uint8_t replace_track,
                                          uint8_t len_mode,
                                          uint32_t expected_frames,
                                          uint8_t play_auto,
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
        | replace_config | len_mode
        | ((uint16_t)(play_auto != 0U)
            << AUDIO_RECORDER_LOOPER_PLAY_AUTO_SHIFT));
    if (control_audio_publish_record(CONTROL_AUDIO_RECORD_START,
            expected_frames, config, track, request_sample) == 0U)
        return 0U;
    g_audio_recorder_looper_control = (audio_recorder_looper_control_t){
        .expected_frames = expected_frames,
        .track = track,
        .start_armed = 1U
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

uint8_t audio_recorder_audio_start(uint8_t client, uint32_t session_id,
                                   uint16_t config_id)
{
    audio_recorder_prepared_config_t *config = NULL;
    for (uint8_t i = 0U; i < 8U; ++i)
        if (g_audio_recorder_config[i].id == config_id)
        { config = &g_audio_recorder_config[i]; break; }
    if ((config == NULL) || (config->client != client)
            || (config->generation != session_id)) return 0U;
    g_audio_recorder_capture.prepared = 0U;
    g_audio_recorder_capture.active = 0U;
    g_audio_recorder_capture.accepted_frames = 0U;
    g_audio_recorder_capture.released_frames = 0U;
    g_audio_recorder_capture.stop_generation = 0U;
    g_audio_recorder_capture.error = AUDIO_RECORDER_ERROR_NONE;
    g_audio_recorder_capture.frame_limit = config->frame_limit;
    g_audio_recorder_capture.client = client;
    g_audio_recorder_capture.generation = session_id;
    config->id = 0U;
    g_audio_recorder_capture.prepared = 1U;
    g_audio_recorder_capture.active = 1U;
    __DMB();
    return 1U;
}

uint8_t audio_recorder_audio_stop(uint8_t client, uint32_t session_id)
{
    if ((g_audio_recorder_capture.client != client)
            || (g_audio_recorder_capture.generation != session_id))
    {
        audio_recorder_prepared_config_t *config = NULL;
        for (uint8_t i = 0U; i < 8U; ++i)
            if ((g_audio_recorder_config[i].id != 0U)
                    && (g_audio_recorder_config[i].client == client)
                    && (g_audio_recorder_config[i].generation == session_id))
            { config = &g_audio_recorder_config[i]; break; }
        if (config == NULL) return 0U;
        g_audio_recorder_capture.client = client;
        g_audio_recorder_capture.generation = session_id;
        g_audio_recorder_capture.frame_limit = config->frame_limit;
        g_audio_recorder_capture.prepared = 1U;
        config->id = 0U;
    }
    g_audio_recorder_capture.active = 0U;
    __DMB();
    g_audio_recorder_capture.stop_generation = session_id;
    return 1U;
}

static void audio_recorder_capture_transport_service(void)
{
    const uint32_t accepted_frames = g_audio_recorder_capture.accepted_frames;
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
    g_audio_recorder_capture.released_frames = committed_frames;
    const uint32_t retained = accepted_frames - committed_frames;
    if (retained > g_audio_recorder.recorder.metrics.ring_high_watermark_frames)
        g_audio_recorder.recorder.metrics.ring_high_watermark_frames = retained;
    const uint32_t free_frames = AUDIO_RECORDER_RING_FRAMES - retained;
    if (free_frames < g_audio_recorder.recorder.metrics.ring_min_free_frames)
        g_audio_recorder.recorder.metrics.ring_min_free_frames = free_frames;
    const uint64_t backlog = accepted_tail
        - g_audio_recorder.recorder.committed_tail;
    if (backlog > g_audio_recorder.recorder.metrics.max_backlog_bytes)
        g_audio_recorder.recorder.metrics.max_backlog_bytes = backlog;

    if (g_audio_recorder_capture.error != AUDIO_RECORDER_ERROR_NONE)
    {
        g_audio_recorder.error =
            (audio_recorder_error_t)g_audio_recorder_capture.error;
        g_audio_recorder.recorder.error = GENERIC_RECORDER_ERROR_RING_FULL;
    }
    if ((g_audio_recorder_capture.stop_generation
            == g_audio_recorder_capture.generation)
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
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        g_audio_recorder_capture.error = g_audio_recorder.error;
        g_audio_recorder_capture.active = 0U;
        __DMB();
        g_audio_recorder_capture.stop_generation =
            g_audio_recorder_capture.generation;
        g_audio_recorder.state = AUDIO_RECORDER_STATE_FAILED;
    }
    else if ((g_audio_recorder.recorder.state == GENERIC_RECORDER_DRAINING)
            && (g_audio_recorder.state == AUDIO_RECORDER_STATE_RECORDING))
    {
        g_audio_recorder.error = audio_recorder_map_error(
            g_audio_recorder.recorder.error);
        if (g_audio_recorder.error != AUDIO_RECORDER_ERROR_NONE)
        {
            g_audio_recorder_capture.error = g_audio_recorder.error;
            g_audio_recorder_capture.active = 0U;
            __DMB();
            g_audio_recorder_capture.stop_generation =
                g_audio_recorder_capture.generation;
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
    audio_recorder_publish_live_stream();
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
    status->frames_received = g_audio_recorder_capture.accepted_frames;
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
    if ((stream == 0) || (client == AUDIO_RECORDER_CLIENT_NONE)) return 0U;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        intercore_cache_consume(&g_audio_recorder_live_publication,
                                sizeof(g_audio_recorder_live_publication));
        const uint32_t before = g_audio_recorder_live_publication.sequence;
        if ((before == 0U) || ((before & 1U) != 0U)
                || (g_audio_recorder_live_publication.valid == 0U)
                || (g_audio_recorder_live_publication.client != (uint8_t)client))
            continue;
        stream->accepted_frames = g_audio_recorder_live_publication.accepted_frames;
        stream->committed_frames = g_audio_recorder_live_publication.committed_frames;
        for (uint32_t i = 0U; i < AUDIO_RECORDER_PATH_MAX; ++i)
            stream->path[i] = g_audio_recorder_live_publication.path[i];
        memcpy(&stream->reservation,
               &g_audio_recorder_live_publication.reservation,
               sizeof(stream->reservation));
        intercore_cache_consume((const void *)&g_audio_recorder_live_publication.sequence,
                                sizeof(g_audio_recorder_live_publication.sequence));
        const uint32_t after = g_audio_recorder_live_publication.sequence;
        if ((before == after) && ((after & 1U) == 0U))
            return 1U;
    }
    return 0U;
}

uint8_t audio_recorder_client_is_active(audio_recorder_client_t client)
{
    return (uint8_t)((g_audio_recorder.client == client)
            && (audio_recorder_is_active() != 0U));
}

uint8_t audio_recorder_client_is_recording(audio_recorder_client_t client)
{
    return (uint8_t)((g_audio_recorder_capture.client == (uint8_t)client)
            && (g_audio_recorder_capture.active != 0U));
}

uint8_t audio_recorder_capture_status_client(audio_recorder_client_t client,
                                             audio_recorder_status_t *status)
{
    if ((status == 0) || (client == AUDIO_RECORDER_CLIENT_NONE)
            || (g_audio_recorder_capture.client != (uint8_t)client)
            || (g_audio_recorder_capture.prepared == 0U))
        return 0U;
    memset(status, 0, sizeof(*status));
    const uint32_t accepted = g_audio_recorder_capture.accepted_frames;
    const uint32_t released = g_audio_recorder_capture.released_frames;
    const uint32_t error = g_audio_recorder_capture.error;
    const uint8_t active = g_audio_recorder_capture.active;
    const uint32_t stopped = g_audio_recorder_capture.stop_generation;
    __DMB();
    status->state = (error != AUDIO_RECORDER_ERROR_NONE)
        ? AUDIO_RECORDER_STATE_FAILED
        : ((active != 0U) ? AUDIO_RECORDER_STATE_RECORDING
                          : ((stopped == g_audio_recorder_capture.generation)
                              ? AUDIO_RECORDER_STATE_DRAINING
                              : AUDIO_RECORDER_STATE_PREPARED));
    status->error = (audio_recorder_error_t)error;
    status->frames_received = accepted;
    status->frames_committed = released;
    status->frames_pending = accepted - released;
    return 1U;
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
