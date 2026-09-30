#include "Storage/audio_recorder_writer.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "Recorder/audio_recorder_ring.h"
#include "SD/sd_block_device.h"
#include "Storage/audio_recorder_format.h"
#include "Platform/stream_rec_perf.h"

#define AUDIO_RECORDER_WRITER_SECTOR_BYTES (512U)
#define AUDIO_RECORDER_WRITER_MAXIMUM_WRITE_BYTES SD_SCHEDULER_SEQUENTIAL_DATA_BYTES
#define AUDIO_RECORDER_WRITER_MINIMUM_WRITE_BYTES SD_SCHEDULER_SEQUENTIAL_DATA_BYTES
#define AUDIO_RECORDER_WRITER_EXTENSION_BYTES (2U * 1024U * 1024U)
#define AUDIO_RECORDER_WRITER_RESERVATION_LOW_US (3000000U)
#define AUDIO_RECORDER_WRITER_RESERVATION_CRITICAL_US (1000000U)
#define AUDIO_RECORDER_WRITER_ESTIMATED_WRITE_US_PER_SECTOR (250U)

_Static_assert((AUDIO_RECORDER_CAPTURE_RING_FRAMES * AUDIO_RECORDER_BYTES_PER_FRAME)
                   % AUDIO_RECORDER_WRITER_SECTOR_BYTES == 0U,
               "Recorder ring must contain whole SD sectors");
_Static_assert(AUDIO_RECORDER_WAV_HEADER_BYTES
                   % AUDIO_RECORDER_WRITER_SECTOR_BYTES == 0U,
               "Recorder WAV header must contain whole SD sectors");

static uint32_t g_audio_recorder_writer_generation;

static uint32_t audio_recorder_writer_next_generation(void)
{
    g_audio_recorder_writer_generation++;
    if (g_audio_recorder_writer_generation == 0U)
    {
        g_audio_recorder_writer_generation = 1U;
    }
    return g_audio_recorder_writer_generation;
}

static uint32_t audio_recorder_writer_bytes_per_second(void)
{
    const uint64_t value =
        (uint64_t)AUDIO_RECORDER_SAMPLE_RATE_HZ
        * AUDIO_RECORDER_BYTES_PER_FRAME;
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t)value;
}

static uint32_t audio_recorder_writer_retained_frames(const audio_recorder_writer_t *recorder)
{
    if (recorder->released_frames > recorder->accepted_frames)
        return AUDIO_RECORDER_CAPTURE_RING_FRAMES;
    const uint64_t retained = recorder->accepted_frames - recorder->released_frames;
    return (retained > AUDIO_RECORDER_CAPTURE_RING_FRAMES)
        ? AUDIO_RECORDER_CAPTURE_RING_FRAMES : (uint32_t)retained;
}

static uint8_t audio_recorder_writer_snapshot(audio_recorder_writer_t *recorder,
                                         recorder_file_reservation_map_snapshot_t *snapshot)
{
    if (recorder_file_reservation_map_snapshot(
            recorder->reservation, snapshot) == 0U)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_RESERVATION;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    if ((snapshot->sector_size != AUDIO_RECORDER_WRITER_SECTOR_BYTES)
        || (snapshot->reserved_file_bytes < AUDIO_RECORDER_WAV_HEADER_BYTES))
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_MAPPING;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    if (snapshot->media_epoch != recorder->media_epoch)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_MEDIA_CHANGED;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    recorder->reserved_capacity =
        snapshot->reserved_file_bytes - AUDIO_RECORDER_WAV_HEADER_BYTES;
    return 1U;
}

static uint8_t audio_recorder_writer_has_descriptors(const audio_recorder_writer_t *recorder)
{
    for (uint32_t i = 0U; i < AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT; ++i)
    {
        if (recorder->descriptors[i].state != AUDIO_RECORDER_WRITER_DESCRIPTOR_FREE)
        {
            return 1U;
        }
    }
    return 0U;
}

static audio_recorder_writer_write_descriptor_t *audio_recorder_writer_free_descriptor(
    audio_recorder_writer_t *recorder)
{
    for (uint32_t i = 0U; i < AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT; ++i)
    {
        if (recorder->descriptors[i].state == AUDIO_RECORDER_WRITER_DESCRIPTOR_FREE)
        {
            return &recorder->descriptors[i];
        }
    }
    return 0;
}

static audio_recorder_writer_write_descriptor_t *audio_recorder_writer_ready_descriptor(
    audio_recorder_writer_t *recorder)
{
    audio_recorder_writer_write_descriptor_t *best = 0;
    for (uint32_t i = 0U; i < AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT; ++i)
    {
        audio_recorder_writer_write_descriptor_t *const descriptor =
            &recorder->descriptors[i];
        if ((descriptor->state == AUDIO_RECORDER_WRITER_DESCRIPTOR_READY)
            && ((best == 0)
                || ((descriptor->logical_offset + descriptor->sent_valid_bytes)
                    < (best->logical_offset + best->sent_valid_bytes))))
        {
            best = descriptor;
        }
    }
    return best;
}

static audio_recorder_writer_write_descriptor_t *audio_recorder_writer_in_flight_descriptor(
    audio_recorder_writer_t *recorder)
{
    for (uint32_t i = 0U; i < AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT; ++i)
    {
        if (recorder->descriptors[i].state
            == AUDIO_RECORDER_WRITER_DESCRIPTOR_IN_FLIGHT)
        {
            return &recorder->descriptors[i];
        }
    }
    return 0;
}

static const uint8_t *audio_recorder_writer_source(audio_recorder_writer_t *recorder,
                                              uint64_t logical_offset,
                                              uint32_t valid_bytes,
                                              uint32_t dma_bytes)
{
    const uint8_t *const ring = (const uint8_t *)recorder->ring_interleaved;
    const uint32_t ring_bytes = AUDIO_RECORDER_CAPTURE_RING_FRAMES
                                * AUDIO_RECORDER_BYTES_PER_FRAME;
    const uint32_t ring_offset = (uint32_t)(logical_offset % ring_bytes);
    if (dma_bytes == valid_bytes)
    {
        return &ring[ring_offset];
    }
    memcpy(recorder->tail_buffer, &ring[ring_offset], valid_bytes);
    memset(&recorder->tail_buffer[valid_bytes], 0, dma_bytes - valid_bytes);
    return recorder->tail_buffer;
}

static uint8_t audio_recorder_writer_prepare_descriptor(audio_recorder_writer_t *recorder,
                                                    uint64_t accepted_tail)
{
    audio_recorder_writer_write_descriptor_t *const descriptor =
        audio_recorder_writer_free_descriptor(recorder);
    const uint64_t backlog = accepted_tail - recorder->assigned_tail;
    if ((descriptor == 0) || (backlog == 0U))
    {
        return 0U;
    }
    if ((recorder->state == AUDIO_RECORDER_WRITER_CAPTURING)
        && (backlog < AUDIO_RECORDER_WRITER_MINIMUM_WRITE_BYTES))
    {
        return 0U;
    }
    uint64_t valid_goal = backlog;
    if (valid_goal > AUDIO_RECORDER_WRITER_MAXIMUM_WRITE_BYTES)
    {
        valid_goal = AUDIO_RECORDER_WRITER_MAXIMUM_WRITE_BYTES;
    }
    const uint32_t ring_bytes = AUDIO_RECORDER_CAPTURE_RING_FRAMES
                                * AUDIO_RECORDER_BYTES_PER_FRAME;
    const uint32_t ring_offset = (uint32_t)(recorder->assigned_tail % ring_bytes);
    const uint32_t contiguous_bytes = ring_bytes - ring_offset;
    if (valid_goal > contiguous_bytes) valid_goal = contiguous_bytes;
    uint32_t dma_goal;
    if (recorder->state == AUDIO_RECORDER_WRITER_CAPTURING)
    {
        dma_goal = (uint32_t)valid_goal & ~(AUDIO_RECORDER_WRITER_SECTOR_BYTES - 1U);
        if (dma_goal == 0U)
        {
            return 0U;
        }
        valid_goal = dma_goal;
    }
    else
    {
        const uint32_t complete_bytes = (uint32_t)valid_goal
            & ~(AUDIO_RECORDER_WRITER_SECTOR_BYTES - 1U);
        if (complete_bytes != 0U)
        {
            valid_goal = complete_bytes;
            dma_goal = complete_bytes;
        }
        else
        {
            dma_goal = AUDIO_RECORDER_WRITER_SECTOR_BYTES;
        }
    }

    recorder_file_reservation_map_snapshot_t snapshot;
    if (audio_recorder_writer_snapshot(recorder, &snapshot) == 0U)
    {
        return 0U;
    }
    const uint64_t file_offset =
        (uint64_t)AUDIO_RECORDER_WAV_HEADER_BYTES + recorder->assigned_tail;
    if (((file_offset & (AUDIO_RECORDER_WRITER_SECTOR_BYTES - 1U)) != 0U)
        || (file_offset + dma_goal > snapshot.reserved_file_bytes))
    {
        return 0U;
    }
    sample_stream_physical_span_t span;
    if ((recorder_file_reservation_map_resolve(
             &snapshot,
             file_offset,
             dma_goal,
             &span) == 0U)
        || (span.first_sector_skip != 0U) || (span.sector_count == 0U))
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_MAPPING;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    uint32_t dma_bytes = span.sector_count * AUDIO_RECORDER_WRITER_SECTOR_BYTES;
    if (dma_bytes > dma_goal)
    {
        dma_bytes = dma_goal;
    }
    uint32_t valid_bytes = (uint32_t)valid_goal;
    if (valid_bytes > dma_bytes)
    {
        valid_bytes = dma_bytes;
    }
    if ((valid_bytes == 0U)
        || ((recorder->state == AUDIO_RECORDER_WRITER_CAPTURING)
            && (valid_bytes != dma_bytes))
        || ((valid_bytes != dma_bytes)
            && ((dma_bytes != AUDIO_RECORDER_WRITER_SECTOR_BYTES)
                || (valid_bytes >= AUDIO_RECORDER_WRITER_SECTOR_BYTES))))
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_MAPPING;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->logical_offset = recorder->assigned_tail;
    descriptor->lba = span.lba;
    descriptor->dma_bytes = dma_bytes;
    descriptor->valid_bytes = valid_bytes;
    descriptor->media_epoch = snapshot.media_epoch;
    PERF_START(pack_start);
    descriptor->buffer = audio_recorder_writer_source(recorder,
                                                 descriptor->logical_offset,
                                                 valid_bytes,
                                                 dma_bytes);
    PERF_END(PERF_CPU_REC_PACK, pack_start);
    descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_READY;
    recorder->assigned_tail += valid_bytes;
    return 1U;
}

void audio_recorder_writer_init(audio_recorder_writer_t *recorder)
{
    if (recorder != 0)
    {
        memset(recorder, 0, sizeof(*recorder));
    }
}

uint8_t audio_recorder_writer_begin(audio_recorder_writer_t *recorder,
                                    const void *ring_interleaved,
                                    uint8_t *tail_buffer,
                                    recorder_file_reservation_t *reservation)
{
    if ((recorder == 0) || (ring_interleaved == 0)
        || (tail_buffer == 0) || (reservation == 0)
        || ((((uintptr_t)tail_buffer) & 31U) != 0U)
        || ((((uintptr_t)ring_interleaved) & 31U) != 0U))
    {
        return 0U;
    }
    audio_recorder_writer_init(recorder);
    recorder->ring_interleaved = ring_interleaved;
    recorder->tail_buffer = tail_buffer;
    recorder->reservation = reservation;
    recorder->generation = audio_recorder_writer_next_generation();
    recorder_file_reservation_map_snapshot_t snapshot;
    if (recorder_file_reservation_map_snapshot(
            reservation, &snapshot) == 0U)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_RESERVATION;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    if ((snapshot.sector_size != AUDIO_RECORDER_WRITER_SECTOR_BYTES)
        || (snapshot.reserved_file_bytes < AUDIO_RECORDER_WAV_HEADER_BYTES))
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_MAPPING;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return 0U;
    }
    recorder->media_epoch = snapshot.media_epoch;
    recorder->reserved_capacity =
        snapshot.reserved_file_bytes - AUDIO_RECORDER_WAV_HEADER_BYTES;
    recorder->state = AUDIO_RECORDER_WRITER_CAPTURING;
    return 1U;
}

uint8_t audio_recorder_writer_request_stop(audio_recorder_writer_t *recorder)
{
    if (recorder == 0)
    {
        return 0U;
    }
    if ((recorder->state == AUDIO_RECORDER_WRITER_DRAINING)
        || (recorder->state == AUDIO_RECORDER_WRITER_FINALIZABLE))
    {
        return 1U;
    }
    if (recorder->state != AUDIO_RECORDER_WRITER_CAPTURING)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_INVALID_STATE;
        return 0U;
    }
    recorder->state = AUDIO_RECORDER_WRITER_DRAINING;
    recorder->extension_pending = 0U;
    recorder_file_reservation_job_cancel(
        recorder->reservation,
        RECORDER_FILE_JOB_OWNER_LIVE_EXTEND);
    return 1U;
}

static uint64_t audio_recorder_writer_ring_margin_us(
    const audio_recorder_writer_t *recorder)
{
    if (recorder == 0)
    {
        return 0U;
    }
    const uint32_t retained = audio_recorder_writer_retained_frames(recorder);
    const uint32_t free_frames = AUDIO_RECORDER_CAPTURE_RING_FRAMES - retained;
    return ((uint64_t)free_frames * 1000000ULL) / AUDIO_RECORDER_SAMPLE_RATE_HZ;
}

static uint64_t audio_recorder_writer_reservation_margin_us(
    const audio_recorder_writer_t *recorder)
{
    if (recorder == 0)
    {
        return 0U;
    }
    const uint32_t bytes_per_second = audio_recorder_writer_bytes_per_second();
    if (bytes_per_second == 0U)
    {
        return 0U;
    }
    const uint64_t accepted_tail = recorder->accepted_tail;
    const uint64_t reserved_capacity = recorder->reserved_capacity;
    const uint64_t remaining = (reserved_capacity > accepted_tail)
                                   ? reserved_capacity - accepted_tail
                                   : 0U;
    return (remaining * 1000000ULL) / bytes_per_second;
}

void audio_recorder_writer_service(audio_recorder_writer_t *recorder)
{
    if ((recorder == 0)
        || ((recorder->state != AUDIO_RECORDER_WRITER_CAPTURING)
            && (recorder->state != AUDIO_RECORDER_WRITER_DRAINING)))
    {
        return;
    }
    PERF_START(service_start);
    recorder_file_reservation_map_snapshot_t snapshot;
    if (audio_recorder_writer_snapshot(recorder, &snapshot) == 0U)
    {
        PERF_END(PERF_CPU_REC_SERVICE, service_start);
        return;
    }
    const uint64_t accepted_tail = recorder->accepted_tail;
    const uint64_t reservation_margin_us =
        audio_recorder_writer_reservation_margin_us(recorder);
    if ((recorder->state == AUDIO_RECORDER_WRITER_CAPTURING)
        && (reservation_margin_us <= AUDIO_RECORDER_WRITER_RESERVATION_LOW_US)
        && (recorder->extension_pending == 0U))
    {
        recorder->extension_pending = 1U;
    }
    PERF_START(prepare_start);
    (void)audio_recorder_writer_prepare_descriptor(recorder, accepted_tail);
    PERF_END(PERF_CPU_REC_PREPARE, prepare_start);
    if ((recorder->state == AUDIO_RECORDER_WRITER_DRAINING)
        && (recorder->committed_tail == accepted_tail)
        && (recorder->assigned_tail == accepted_tail)
        && (audio_recorder_writer_has_descriptors(recorder) == 0U))
    {
        recorder->state = AUDIO_RECORDER_WRITER_FINALIZABLE;
    }
    PERF_END(PERF_CPU_REC_SERVICE, service_start);
}

void audio_recorder_writer_abort(audio_recorder_writer_t *recorder)
{
    if (recorder != 0)
    {
        recorder->state = AUDIO_RECORDER_WRITER_ABORTED;
    }
}

static uint8_t audio_recorder_writer_write_peek(void *context,
                                           sd_scheduler_candidate_t *candidate)
{
    audio_recorder_writer_t *const recorder = context;
    audio_recorder_writer_write_descriptor_t *const descriptor =
        audio_recorder_writer_ready_descriptor(recorder);
    if ((descriptor == 0) || (candidate == 0)
        || ((recorder->state != AUDIO_RECORDER_WRITER_CAPTURING)
            && (recorder->state != AUDIO_RECORDER_WRITER_DRAINING)))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_WRITE;
    candidate->ready = 1U;
    const uint64_t margin = audio_recorder_writer_ring_margin_us(recorder);
    candidate->margin_us = (margin > UINT32_MAX) ? UINT32_MAX : (uint32_t)margin;
    candidate->lba = descriptor->lba
                     + (descriptor->sent_dma_bytes / AUDIO_RECORDER_WRITER_SECTOR_BYTES);
    candidate->sector_count =
        (descriptor->dma_bytes - descriptor->sent_dma_bytes)
        / AUDIO_RECORDER_WRITER_SECTOR_BYTES;
    candidate->write_buffer = descriptor->buffer + descriptor->sent_dma_bytes;
    candidate->owner_generation = recorder->generation;
    candidate->media_epoch = descriptor->media_epoch;
    const uint64_t cost =
        (uint64_t)candidate->sector_count
        * AUDIO_RECORDER_WRITER_ESTIMATED_WRITE_US_PER_SECTOR;
    candidate->estimated_cost_us =
        (cost > UINT32_MAX) ? UINT32_MAX : (uint32_t)cost;
    return 1U;
}

static sd_scheduler_start_result_t audio_recorder_writer_write_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    audio_recorder_writer_t *const recorder = context;
    audio_recorder_writer_write_descriptor_t *const descriptor =
        audio_recorder_writer_ready_descriptor(recorder);
    if ((descriptor == 0) || (candidate == 0) || (granted_sector_count == 0U)
        || (candidate->owner_generation != recorder->generation)
        || (candidate->media_epoch != recorder->media_epoch)
        || (candidate->lba
            != descriptor->lba
                   + descriptor->sent_dma_bytes / AUDIO_RECORDER_WRITER_SECTOR_BYTES)
        || (granted_sector_count
            > (descriptor->dma_bytes - descriptor->sent_dma_bytes)
                  / AUDIO_RECORDER_WRITER_SECTOR_BYTES))
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_INVALID_ARGUMENT;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return SD_SCHEDULER_START_ERROR;
    }
    const uint32_t dma_bytes = granted_sector_count * AUDIO_RECORDER_WRITER_SECTOR_BYTES;
    const uint32_t remaining_valid = descriptor->valid_bytes
                                     - descriptor->sent_valid_bytes;
    descriptor->active_dma_bytes = dma_bytes;
    descriptor->active_valid_bytes =
        (remaining_valid < dma_bytes) ? remaining_valid : dma_bytes;
    PERF_START(write_start);
    const sd_block_device_result_t result = sd_block_device_async_write_submit(
        candidate->lba,
        granted_sector_count,
        candidate->write_buffer,
        recorder->generation);
    PERF_END(PERF_CPU_REC_WRITE_START, write_start);
    if ((result == SD_BLOCK_DEVICE_BUSY)
        || (result == SD_BLOCK_DEVICE_QUEUE_FULL))
    {
        descriptor->active_dma_bytes = 0U;
        descriptor->active_valid_bytes = 0U;
        return SD_SCHEDULER_START_BUSY;
    }
    if (result != SD_BLOCK_DEVICE_OK)
    {
        descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_FAILED;
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_TRANSPORT;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return SD_SCHEDULER_START_ERROR;
    }
    descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_IN_FLIGHT;
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t audio_recorder_writer_write_poll(void *context)
{
    audio_recorder_writer_t *const recorder = context;
    audio_recorder_writer_write_descriptor_t *const descriptor =
        audio_recorder_writer_in_flight_descriptor(recorder);
    if (descriptor == 0)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_INVALID_STATE;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    memset(&completion, 0, sizeof(completion));
    if (sd_block_device_async_take_completion(&completion) == 0U)
    {
        return (sd_block_device_async_hardware_state()
                == SD_BLOCK_DEVICE_HW_ABORTING)
            ? SD_SCHEDULER_POLL_RECOVERY_ABORT : SD_SCHEDULER_POLL_ACTIVE;
    }
    PERF_START(write_complete);
    if ((completion.result != SD_BLOCK_DEVICE_OK)
        || (completion.operation != SD_BLOCK_DEVICE_OPERATION_WRITE)
        || (completion.owner_generation != recorder->generation)
        || (completion.media_epoch != recorder->media_epoch)
        || (completion.lba
            != descriptor->lba
                   + descriptor->sent_dma_bytes / AUDIO_RECORDER_WRITER_SECTOR_BYTES)
        || (completion.sector_count
            != descriptor->active_dma_bytes / AUDIO_RECORDER_WRITER_SECTOR_BYTES)
        || (completion.src
            != descriptor->buffer + descriptor->sent_dma_bytes))
    {
        descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_FAILED;
        recorder->error = ((completion.result == SD_BLOCK_DEVICE_MEDIA_CHANGED)
                            || (completion.result == SD_BLOCK_DEVICE_CARD_REMOVED))
                              ? AUDIO_RECORDER_WRITER_ERROR_MEDIA_CHANGED
                              : AUDIO_RECORDER_WRITER_ERROR_WRITE;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    const uint64_t expected = descriptor->logical_offset
                              + descriptor->sent_valid_bytes;
    if (recorder->committed_tail != expected)
    {
        descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_FAILED;
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_WRITE;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    PERF_COUNT(PERF_N_REC_WRITES);
    PERF_ACCUM(PERF_N_REC_WRITE_BYTES, descriptor->active_dma_bytes);
    PERF_MIN_NONZERO(PERF_N_REC_WRITE_MIN_BYTES, descriptor->active_dma_bytes);
    PERF_MAX(PERF_N_REC_WRITE_MAX_BYTES, descriptor->active_dma_bytes);
    descriptor->sent_dma_bytes += descriptor->active_dma_bytes;
    descriptor->sent_valid_bytes += descriptor->active_valid_bytes;
    recorder->committed_tail += descriptor->active_valid_bytes;
    descriptor->active_dma_bytes = 0U;
    descriptor->active_valid_bytes = 0U;
    PERF_END(PERF_CPU_REC_WRITE_COMPLETE, write_complete);
    if (descriptor->sent_dma_bytes == descriptor->dma_bytes)
    {
        if (descriptor->sent_valid_bytes != descriptor->valid_bytes)
        {
            descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_FAILED;
            recorder->error = AUDIO_RECORDER_WRITER_ERROR_WRITE;
            recorder->state = AUDIO_RECORDER_WRITER_ERROR;
            return SD_SCHEDULER_POLL_ERROR;
        }
        memset(descriptor, 0, sizeof(*descriptor));
    }
    else
    {
        descriptor->state = AUDIO_RECORDER_WRITER_DESCRIPTOR_READY;
    }
    return SD_SCHEDULER_POLL_COMPLETED;
}

static uint8_t audio_recorder_writer_filesystem_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    audio_recorder_writer_t *const recorder = context;
    const recorder_file_job_owner_t job_owner =
        recorder_file_reservation_job_owner(
        recorder->reservation);
    const uint8_t live_job = (job_owner == RECORDER_FILE_JOB_OWNER_LIVE_EXTEND)
        ? 1U : 0U;
    if ((candidate == 0)
        || ((job_owner != RECORDER_FILE_JOB_OWNER_NONE) && (live_job == 0U))
        || ((recorder->extension_pending == 0U) && (live_job == 0U))
        || ((recorder->state != AUDIO_RECORDER_WRITER_CAPTURING) && (live_job == 0U)))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_FILESYSTEM;
    candidate->ready = 1U;
    const uint64_t margin = audio_recorder_writer_reservation_margin_us(recorder);
    candidate->margin_us = (margin > UINT32_MAX) ? UINT32_MAX : (uint32_t)margin;
    candidate->estimated_cost_us = 100000U;
    candidate->owner_generation = recorder->generation;
    candidate->media_epoch = recorder->media_epoch;
    candidate->reservation =
        (margin <= AUDIO_RECORDER_WRITER_RESERVATION_CRITICAL_US)
            ? SD_SCHEDULER_RESERVATION_CRITICAL
            : SD_SCHEDULER_RESERVATION_LOW;
    return 1U;
}

static recorder_file_reservation_result_t
audio_recorder_writer_extend_step(recorder_file_reservation_t *reservation)
{
    const recorder_file_job_owner_t owner =
        recorder_file_reservation_job_owner(reservation);
    if (owner == RECORDER_FILE_JOB_OWNER_NONE)
    {
        const recorder_file_reservation_result_t begun =
            recorder_file_reservation_extend_begin(reservation,
                AUDIO_RECORDER_WRITER_EXTENSION_BYTES,
                RECORDER_FILE_JOB_OWNER_LIVE_EXTEND);
        if (begun != RECORDER_FILE_RESERVATION_OK) return begun;
    }
    else if (owner != RECORDER_FILE_JOB_OWNER_LIVE_EXTEND)
    {
        return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    const recorder_file_reservation_result_t result =
        recorder_file_reservation_job_step(reservation,
            RECORDER_FILE_JOB_OWNER_LIVE_EXTEND);
    if (reservation->job_phase == RECORDER_FILE_JOB_TERMINAL)
    {
        if (recorder_file_reservation_job_finish(reservation,
                RECORDER_FILE_JOB_OWNER_LIVE_EXTEND) == 0U)
            return RECORDER_FILE_RESERVATION_INVALID_STATE;
    }
    return result;
}

static sd_scheduler_start_result_t audio_recorder_writer_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    audio_recorder_writer_t *const recorder = context;
    (void)granted_sector_count;
    const recorder_file_job_owner_t job_owner =
        recorder_file_reservation_job_owner(
        recorder->reservation);
    const uint8_t live_job = (job_owner == RECORDER_FILE_JOB_OWNER_LIVE_EXTEND)
        ? 1U : 0U;
    if ((candidate == 0)
        || ((job_owner != RECORDER_FILE_JOB_OWNER_NONE) && (live_job == 0U))
        || ((recorder->extension_pending == 0U) && (live_job == 0U))
        || (candidate->owner_generation != recorder->generation)
        || (candidate->media_epoch != recorder->media_epoch))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    const recorder_file_reservation_result_t result =
        audio_recorder_writer_extend_step(recorder->reservation);
    if (result == RECORDER_FILE_RESERVATION_IO_STARTED)
    {
        return SD_SCHEDULER_START_STARTED;
    }
    if (result == RECORDER_FILE_RESERVATION_PROGRESS)
    {
        return SD_SCHEDULER_START_COMPLETED;
    }
    if (result == RECORDER_FILE_RESERVATION_SD_BUSY)
    {
        return SD_SCHEDULER_START_BUSY;
    }
    if ((result != RECORDER_FILE_RESERVATION_OK)
        && (result != RECORDER_FILE_RESERVATION_PARTIAL))
    {
        recorder->error = (result == RECORDER_FILE_RESERVATION_NO_SPACE)
                              ? AUDIO_RECORDER_WRITER_ERROR_NO_SPACE
                              : AUDIO_RECORDER_WRITER_ERROR_RESERVATION;
        recorder->state = (result == RECORDER_FILE_RESERVATION_NO_SPACE)
                              ? AUDIO_RECORDER_WRITER_DRAINING
                              : AUDIO_RECORDER_WRITER_ERROR;
        recorder->extension_pending = 0U;
        return SD_SCHEDULER_START_ERROR;
    }
    recorder_file_reservation_map_snapshot_t snapshot;
    if (audio_recorder_writer_snapshot(recorder, &snapshot) == 0U)
    {
        return SD_SCHEDULER_START_ERROR;
    }
    recorder->extension_pending = 0U;
    return SD_SCHEDULER_START_COMPLETED;
}

static sd_scheduler_poll_result_t audio_recorder_writer_filesystem_poll(void *context)
{
    audio_recorder_writer_t *const recorder = context;
    if ((recorder == 0) || (recorder->reservation == 0))
    {
        return SD_SCHEDULER_POLL_ERROR;
    }
    const recorder_file_reservation_result_t result =
        recorder_file_reservation_job_poll(recorder->reservation,
            RECORDER_FILE_JOB_OWNER_LIVE_EXTEND);
    if (result == RECORDER_FILE_RESERVATION_IO_STARTED)
    {
        return SD_SCHEDULER_POLL_ACTIVE;
    }
    if (result == RECORDER_FILE_RESERVATION_RECOVERY_ABORT)
    {
        return SD_SCHEDULER_POLL_RECOVERY_ABORT;
    }
    if (result == RECORDER_FILE_RESERVATION_PROGRESS)
    {
        return SD_SCHEDULER_POLL_COMPLETED;
    }
    if (recorder_file_reservation_job_finish(
            recorder->reservation,
            RECORDER_FILE_JOB_OWNER_LIVE_EXTEND) == 0U)
    {
        recorder->error = AUDIO_RECORDER_WRITER_ERROR_RESERVATION;
        recorder->state = AUDIO_RECORDER_WRITER_ERROR;
        recorder->extension_pending = 0U;
        return SD_SCHEDULER_POLL_ERROR;
    }
    recorder->error = (result == RECORDER_FILE_RESERVATION_NO_SPACE)
        ? AUDIO_RECORDER_WRITER_ERROR_NO_SPACE : AUDIO_RECORDER_WRITER_ERROR_RESERVATION;
    recorder->state = AUDIO_RECORDER_WRITER_ERROR;
    recorder->extension_pending = 0U;
    return SD_SCHEDULER_POLL_ERROR;
}

sd_scheduler_provider_t audio_recorder_writer_write_provider(audio_recorder_writer_t *recorder)
{
    const sd_scheduler_provider_t provider = {
        .context = recorder,
        .peek = audio_recorder_writer_write_peek,
        .start = audio_recorder_writer_write_start,
        .poll = audio_recorder_writer_write_poll,
    };
    return provider;
}

sd_scheduler_provider_t audio_recorder_writer_filesystem_provider(
    audio_recorder_writer_t *recorder)
{
    const sd_scheduler_provider_t provider = {
        .context = recorder,
        .peek = audio_recorder_writer_filesystem_peek,
        .start = audio_recorder_writer_filesystem_start,
        .poll = audio_recorder_writer_filesystem_poll,
    };
    return provider;
}
