#include "Storage/generic_recorder.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define GENERIC_RECORDER_SECTOR_BYTES (512U)

static uint32_t g_generic_recorder_generation;

static uint32_t generic_recorder_next_generation(void)
{
    g_generic_recorder_generation++;
    if (g_generic_recorder_generation == 0U)
    {
        g_generic_recorder_generation = 1U;
    }
    return g_generic_recorder_generation;
}

static uint32_t generic_recorder_bytes_per_frame(const generic_recorder_t *recorder)
{
    return (uint32_t)recorder->config.channels * 3U;
}

static uint32_t generic_recorder_bytes_per_second(const generic_recorder_t *recorder)
{
    const uint64_t value =
        (uint64_t)recorder->config.sample_rate_hz
        * generic_recorder_bytes_per_frame(recorder);
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t)value;
}

static void generic_recorder_critical_enter(const generic_recorder_t *recorder)
{
    if (recorder->config.critical_enter != 0)
    {
        recorder->config.critical_enter(recorder->config.critical_context);
    }
}

static void generic_recorder_critical_exit(const generic_recorder_t *recorder)
{
    if (recorder->config.critical_exit != 0)
    {
        recorder->config.critical_exit(recorder->config.critical_context);
    }
}

static uint64_t generic_recorder_accepted_snapshot(
    const generic_recorder_t *recorder)
{
    generic_recorder_critical_enter(recorder);
    const uint64_t accepted = recorder->accepted_tail;
    generic_recorder_critical_exit(recorder);
    return accepted;
}

static uint32_t generic_recorder_retained_frames(const generic_recorder_t *recorder)
{
    const uint32_t bytes_per_frame = generic_recorder_bytes_per_frame(recorder);
    const uint64_t first_retained_frame = recorder->committed_tail / bytes_per_frame;
    const uint64_t retained = recorder->accepted_frames - first_retained_frame;
    return (retained > UINT32_MAX) ? UINT32_MAX : (uint32_t)retained;
}

static uint8_t generic_recorder_snapshot(generic_recorder_t *recorder,
                                         recorder_file_reservation_map_snapshot_t *snapshot)
{
    if (recorder->config.reservation.snapshot(
            recorder->config.reservation.context, snapshot) == 0U)
    {
        recorder->error = GENERIC_RECORDER_ERROR_RESERVATION;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    if ((snapshot->sector_size != GENERIC_RECORDER_SECTOR_BYTES)
        || (snapshot->reserved_file_bytes < recorder->config.reserved_header_bytes))
    {
        recorder->error = GENERIC_RECORDER_ERROR_MAPPING;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    if (snapshot->media_epoch != recorder->media_epoch)
    {
        recorder->error = GENERIC_RECORDER_ERROR_MEDIA_CHANGED;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    generic_recorder_critical_enter(recorder);
    recorder->reserved_capacity =
        snapshot->reserved_file_bytes - recorder->config.reserved_header_bytes;
    generic_recorder_critical_exit(recorder);
    return 1U;
}

static uint8_t generic_recorder_has_descriptors(const generic_recorder_t *recorder)
{
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
    {
        if (recorder->descriptors[i].state != GENERIC_RECORDER_DESCRIPTOR_FREE)
        {
            return 1U;
        }
    }
    return 0U;
}

static generic_recorder_write_descriptor_t *generic_recorder_free_descriptor(
    generic_recorder_t *recorder)
{
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
    {
        if (recorder->descriptors[i].state == GENERIC_RECORDER_DESCRIPTOR_FREE)
        {
            return &recorder->descriptors[i];
        }
    }
    return 0;
}

static generic_recorder_write_descriptor_t *generic_recorder_ready_descriptor(
    generic_recorder_t *recorder)
{
    generic_recorder_write_descriptor_t *best = 0;
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
    {
        generic_recorder_write_descriptor_t *const descriptor =
            &recorder->descriptors[i];
        if ((descriptor->state == GENERIC_RECORDER_DESCRIPTOR_READY)
            && ((best == 0)
                || ((descriptor->logical_offset + descriptor->sent_valid_bytes)
                    < (best->logical_offset + best->sent_valid_bytes))))
        {
            best = descriptor;
        }
    }
    return best;
}

static generic_recorder_write_descriptor_t *generic_recorder_in_flight_descriptor(
    generic_recorder_t *recorder)
{
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
    {
        if (recorder->descriptors[i].state
            == GENERIC_RECORDER_DESCRIPTOR_IN_FLIGHT)
        {
            return &recorder->descriptors[i];
        }
    }
    return 0;
}

static void generic_recorder_pack(generic_recorder_t *recorder,
                                  uint8_t *destination,
                                  uint64_t logical_offset,
                                  uint32_t valid_bytes,
                                  uint32_t dma_bytes)
{
    const uint32_t bytes_per_frame = generic_recorder_bytes_per_frame(recorder);
    uint64_t frame = logical_offset / bytes_per_frame;
    uint32_t byte_in_frame = (uint32_t)(logical_offset % bytes_per_frame);
    uint32_t output = 0U;
    while ((output < valid_bytes) && (byte_in_frame != 0U))
    {
        const uint32_t channel = byte_in_frame / 3U;
        const uint32_t byte_in_sample = byte_in_frame % 3U;
        const uint32_t ring_frame =
            (uint32_t)(frame % recorder->config.ring_capacity_frames);
        const int32_t value =
            recorder->config.ring_interleaved[
                ring_frame * recorder->config.channels + channel];
        destination[output++] =
            (uint8_t)((uint32_t)value >> (byte_in_sample * 8U));
        byte_in_frame++;
        if (byte_in_frame == bytes_per_frame)
        {
            byte_in_frame = 0U;
            frame++;
        }
    }
    while ((valid_bytes - output) >= bytes_per_frame)
    {
        const uint32_t ring_frame =
            (uint32_t)(frame % recorder->config.ring_capacity_frames);
        for (uint32_t channel = 0U;
             channel < recorder->config.channels;
             ++channel)
        {
            const uint32_t value = (uint32_t)
                recorder->config.ring_interleaved[
                    ring_frame * recorder->config.channels + channel];
            destination[output++] = (uint8_t)value;
            destination[output++] = (uint8_t)(value >> 8);
            destination[output++] = (uint8_t)(value >> 16);
        }
        frame++;
    }
    byte_in_frame = 0U;
    while (output < valid_bytes)
    {
        const uint32_t channel = byte_in_frame / 3U;
        const uint32_t byte_in_sample = byte_in_frame % 3U;
        const uint32_t ring_frame =
            (uint32_t)(frame % recorder->config.ring_capacity_frames);
        const uint32_t value = (uint32_t)
            recorder->config.ring_interleaved[
                ring_frame * recorder->config.channels + channel];
        destination[output++] =
            (uint8_t)(value >> (byte_in_sample * 8U));
        byte_in_frame++;
    }
    if (dma_bytes > valid_bytes)
    {
        memset(&destination[valid_bytes], 0, dma_bytes - valid_bytes);
    }
    recorder->metrics.bytes_packed += valid_bytes;
}

static uint8_t generic_recorder_prepare_descriptor(generic_recorder_t *recorder,
                                                    uint64_t accepted_tail)
{
    generic_recorder_write_descriptor_t *const descriptor =
        generic_recorder_free_descriptor(recorder);
    const uint64_t backlog = accepted_tail - recorder->assigned_tail;
    if ((descriptor == 0) || (backlog == 0U))
    {
        return 0U;
    }
    if ((recorder->state == GENERIC_RECORDER_CAPTURING)
        && (backlog < recorder->config.minimum_write_bytes))
    {
        return 0U;
    }
    uint64_t valid_goal = backlog;
    if (valid_goal > recorder->config.write_buffer_bytes)
    {
        valid_goal = recorder->config.write_buffer_bytes;
    }
    uint32_t dma_goal;
    if (recorder->state == GENERIC_RECORDER_CAPTURING)
    {
        dma_goal = (uint32_t)valid_goal & ~(GENERIC_RECORDER_SECTOR_BYTES - 1U);
        if (dma_goal == 0U)
        {
            return 0U;
        }
        valid_goal = dma_goal;
    }
    else
    {
        dma_goal = (uint32_t)((valid_goal + GENERIC_RECORDER_SECTOR_BYTES - 1U)
                              & ~(uint64_t)(GENERIC_RECORDER_SECTOR_BYTES - 1U));
    }

    recorder_file_reservation_map_snapshot_t snapshot;
    if (generic_recorder_snapshot(recorder, &snapshot) == 0U)
    {
        return 0U;
    }
    const uint64_t file_offset =
        (uint64_t)recorder->config.reserved_header_bytes + recorder->assigned_tail;
    if (((file_offset & (GENERIC_RECORDER_SECTOR_BYTES - 1U)) != 0U)
        || (file_offset + dma_goal > snapshot.reserved_file_bytes))
    {
        return 0U;
    }
    sample_stream_physical_span_t span;
    if ((recorder->config.reservation.resolve(
             recorder->config.reservation.context,
             &snapshot,
             file_offset,
             dma_goal,
             &span) == 0U)
        || (span.first_sector_skip != 0U) || (span.sector_count == 0U))
    {
        recorder->error = GENERIC_RECORDER_ERROR_MAPPING;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    uint32_t dma_bytes = span.sector_count * GENERIC_RECORDER_SECTOR_BYTES;
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
        || ((recorder->state == GENERIC_RECORDER_CAPTURING)
            && (valid_bytes != dma_bytes)))
    {
        recorder->error = GENERIC_RECORDER_ERROR_MAPPING;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }

    const uint32_t buffer_index = (uint32_t)(descriptor - recorder->descriptors);
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->buffer = recorder->config.write_buffers[buffer_index];
    descriptor->logical_offset = recorder->assigned_tail;
    descriptor->lba = span.lba;
    descriptor->dma_bytes = dma_bytes;
    descriptor->valid_bytes = valid_bytes;
    descriptor->media_epoch = snapshot.media_epoch;
    descriptor->extent_index = span.extent_index;
    generic_recorder_pack(recorder,
                          descriptor->buffer,
                          descriptor->logical_offset,
                          valid_bytes,
                          dma_bytes);
    descriptor->state = GENERIC_RECORDER_DESCRIPTOR_READY;
    if ((recorder->last_extent_valid != 0U)
        && (recorder->last_extent_index != span.extent_index))
    {
        recorder->metrics.extent_crossings++;
    }
    recorder->last_extent_index = span.extent_index;
    recorder->last_extent_valid = 1U;
    recorder->assigned_tail += valid_bytes;
    recorder->metrics.bytes_assigned = recorder->assigned_tail;
    recorder->metrics.write_candidates++;
    return 1U;
}

void generic_recorder_init(generic_recorder_t *recorder)
{
    if (recorder != 0)
    {
        memset(recorder, 0, sizeof(*recorder));
    }
}

uint8_t generic_recorder_begin(generic_recorder_t *recorder,
                               const generic_recorder_config_t *config)
{
    if ((recorder == 0) || (config == 0) || (config->ring_interleaved == 0)
        || (config->ring_capacity_frames == 0U)
        || (config->sample_rate_hz == 0U) || (config->channels == 0U)
        || (config->write_buffer_bytes < GENERIC_RECORDER_SECTOR_BYTES)
        || ((config->write_buffer_bytes
             & (GENERIC_RECORDER_SECTOR_BYTES - 1U)) != 0U)
        || (config->minimum_write_bytes < GENERIC_RECORDER_SECTOR_BYTES)
        || (config->minimum_write_bytes > config->write_buffer_bytes)
        || ((config->minimum_write_bytes
             & (GENERIC_RECORDER_SECTOR_BYTES - 1U)) != 0U)
        || (config->reserved_header_bytes == 0U)
        || ((config->reserved_header_bytes
             & (GENERIC_RECORDER_SECTOR_BYTES - 1U)) != 0U)
        || (config->extension_bytes == 0U)
        || (config->transport.start == 0) || (config->transport.poll == 0)
        || (config->reservation.snapshot == 0)
        || (config->reservation.resolve == 0)
        || (config->reservation.extend == 0))
    {
        return 0U;
    }
    for (uint32_t i = 0U; i < GENERIC_RECORDER_WRITE_BUFFER_COUNT; ++i)
    {
        if ((config->write_buffers[i] == 0)
            || ((((uintptr_t)config->write_buffers[i]) & 31U) != 0U))
        {
            return 0U;
        }
    }
    generic_recorder_init(recorder);
    recorder->config = *config;
    recorder->generation = generic_recorder_next_generation();
    recorder_file_reservation_map_snapshot_t snapshot;
    if (config->reservation.snapshot(config->reservation.context, &snapshot) == 0U)
    {
        recorder->error = GENERIC_RECORDER_ERROR_RESERVATION;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    if ((snapshot.sector_size != GENERIC_RECORDER_SECTOR_BYTES)
        || (snapshot.reserved_file_bytes < config->reserved_header_bytes))
    {
        recorder->error = GENERIC_RECORDER_ERROR_MAPPING;
        recorder->state = GENERIC_RECORDER_ERROR;
        return 0U;
    }
    recorder->media_epoch = snapshot.media_epoch;
    recorder->reserved_capacity =
        snapshot.reserved_file_bytes - config->reserved_header_bytes;
    recorder->metrics.ring_min_free_frames = config->ring_capacity_frames;
    recorder->metrics.reservation_min_margin_bytes = recorder->reserved_capacity;
    recorder->state = GENERIC_RECORDER_CAPTURING;
    return 1U;
}

uint8_t generic_recorder_push(generic_recorder_t *recorder,
                              const int32_t *pcm_interleaved,
                              uint32_t frames)
{
    if ((recorder == 0) || (pcm_interleaved == 0) || (frames == 0U)
        || (recorder->state != GENERIC_RECORDER_CAPTURING))
    {
        return 0U;
    }
    const uint32_t retained = generic_recorder_retained_frames(recorder);
    const uint64_t additional_bytes =
        (uint64_t)frames * generic_recorder_bytes_per_frame(recorder);
    if ((frames > (recorder->config.ring_capacity_frames - retained))
        || (additional_bytes > recorder->reserved_capacity - recorder->accepted_tail))
    {
        const uint8_t ring_full =
            (frames > (recorder->config.ring_capacity_frames - retained))
                ? 1U : 0U;
        if (ring_full != 0U)
        {
            recorder->metrics.ring_full_rejects++;
        }
        recorder->error = (ring_full != 0U)
            ? GENERIC_RECORDER_ERROR_RING_FULL : GENERIC_RECORDER_ERROR_NO_SPACE;
        recorder->state = GENERIC_RECORDER_DRAINING;
        return 0U;
    }
    uint32_t first_frames = recorder->config.ring_capacity_frames
        - recorder->write_frame_index;
    if (first_frames > frames)
    {
        first_frames = frames;
    }
    const size_t bytes_per_ring_frame =
        (size_t)recorder->config.channels * sizeof(int32_t);
    memcpy(&recorder->config.ring_interleaved[
               recorder->write_frame_index * recorder->config.channels],
           pcm_interleaved,
           (size_t)first_frames * bytes_per_ring_frame);
    const uint32_t second_frames = frames - first_frames;
    if (second_frames != 0U)
    {
        memcpy(recorder->config.ring_interleaved,
               &pcm_interleaved[first_frames * recorder->config.channels],
               (size_t)second_frames * bytes_per_ring_frame);
    }
    recorder->write_frame_index += first_frames;
    if (recorder->write_frame_index == recorder->config.ring_capacity_frames)
    {
        recorder->write_frame_index = second_frames;
    }
    recorder->accepted_frames += frames;
    recorder->accepted_tail += additional_bytes;
    recorder->metrics.frames_accepted = recorder->accepted_frames;
    recorder->metrics.bytes_accepted = recorder->accepted_tail;
    const uint32_t now_retained = generic_recorder_retained_frames(recorder);
    const uint32_t free_frames = recorder->config.ring_capacity_frames - now_retained;
    if (now_retained > recorder->metrics.ring_high_watermark_frames)
    {
        recorder->metrics.ring_high_watermark_frames = now_retained;
    }
    if (free_frames < recorder->metrics.ring_min_free_frames)
    {
        recorder->metrics.ring_min_free_frames = free_frames;
    }
    const uint64_t backlog = recorder->accepted_tail - recorder->committed_tail;
    if (backlog > recorder->metrics.max_backlog_bytes)
    {
        recorder->metrics.max_backlog_bytes = backlog;
    }
    return 1U;
}

uint8_t generic_recorder_request_stop(generic_recorder_t *recorder,
                                      uint32_t now_us)
{
    if (recorder == 0)
    {
        return 0U;
    }
    if ((recorder->state == GENERIC_RECORDER_DRAINING)
        || (recorder->state == GENERIC_RECORDER_FINALIZABLE))
    {
        return 1U;
    }
    if (recorder->state != GENERIC_RECORDER_CAPTURING)
    {
        recorder->error = GENERIC_RECORDER_ERROR_INVALID_STATE;
        return 0U;
    }
    recorder->state = GENERIC_RECORDER_DRAINING;
    recorder->stop_started_us = now_us;
    return 1U;
}

uint64_t generic_recorder_ring_margin_us(const generic_recorder_t *recorder)
{
    if ((recorder == 0) || (recorder->config.sample_rate_hz == 0U))
    {
        return 0U;
    }
    generic_recorder_critical_enter(recorder);
    const uint32_t retained = generic_recorder_retained_frames(recorder);
    generic_recorder_critical_exit(recorder);
    const uint32_t free_frames = recorder->config.ring_capacity_frames - retained;
    return ((uint64_t)free_frames * 1000000ULL) / recorder->config.sample_rate_hz;
}

uint64_t generic_recorder_reservation_margin_us(const generic_recorder_t *recorder)
{
    if (recorder == 0)
    {
        return 0U;
    }
    const uint32_t bytes_per_second = generic_recorder_bytes_per_second(recorder);
    if (bytes_per_second == 0U)
    {
        return 0U;
    }
    generic_recorder_critical_enter(recorder);
    const uint64_t accepted_tail = recorder->accepted_tail;
    const uint64_t reserved_capacity = recorder->reserved_capacity;
    generic_recorder_critical_exit(recorder);
    const uint64_t remaining = (reserved_capacity > accepted_tail)
                                   ? reserved_capacity - accepted_tail
                                   : 0U;
    return (remaining * 1000000ULL) / bytes_per_second;
}

uint8_t generic_recorder_invariants_hold(const generic_recorder_t *recorder)
{
    if (recorder == 0)
    {
        return 0U;
    }
    return ((recorder->committed_tail <= recorder->assigned_tail)
            && (recorder->assigned_tail <= recorder->accepted_tail)
            && (recorder->accepted_tail <= recorder->reserved_capacity))
               ? 1U
               : 0U;
}

void generic_recorder_get_status(const generic_recorder_t *recorder,
                                 generic_recorder_status_t *status)
{
    if ((recorder == 0) || (status == 0))
    {
        return;
    }
    generic_recorder_critical_enter(recorder);
    status->state = recorder->state;
    status->error = recorder->error;
    status->accepted_tail = recorder->accepted_tail;
    status->assigned_tail = recorder->assigned_tail;
    status->committed_tail = recorder->committed_tail;
    status->reserved_capacity = recorder->reserved_capacity;
    generic_recorder_critical_exit(recorder);
    status->ring_margin_us = generic_recorder_ring_margin_us(recorder);
    status->reservation_margin_us =
        generic_recorder_reservation_margin_us(recorder);
    status->accepting =
        (recorder->state == GENERIC_RECORDER_CAPTURING) ? 1U : 0U;
    status->fully_committed =
        (recorder->committed_tail == recorder->accepted_tail) ? 1U : 0U;
}

void generic_recorder_get_metrics(const generic_recorder_t *recorder,
                                  generic_recorder_metrics_t *metrics)
{
    if ((recorder != 0) && (metrics != 0))
    {
        *metrics = recorder->metrics;
    }
}

void generic_recorder_service(generic_recorder_t *recorder, uint32_t now_us)
{
    if ((recorder == 0)
        || ((recorder->state != GENERIC_RECORDER_CAPTURING)
            && (recorder->state != GENERIC_RECORDER_DRAINING)))
    {
        return;
    }
    recorder_file_reservation_map_snapshot_t snapshot;
    if (generic_recorder_snapshot(recorder, &snapshot) == 0U)
    {
        return;
    }
    const uint64_t accepted_tail = generic_recorder_accepted_snapshot(recorder);
    const uint64_t reservation_margin =
        (recorder->reserved_capacity > accepted_tail)
            ? recorder->reserved_capacity - accepted_tail
            : 0U;
    if (reservation_margin < recorder->metrics.reservation_min_margin_bytes)
    {
        recorder->metrics.reservation_min_margin_bytes = reservation_margin;
    }
    const uint64_t reservation_margin_us =
        generic_recorder_reservation_margin_us(recorder);
    if ((recorder->state == GENERIC_RECORDER_CAPTURING)
        && (reservation_margin_us <= recorder->config.reservation_low_margin_us)
        && (recorder->extension_pending == 0U))
    {
        recorder->extension_pending = 1U;
        recorder->metrics.extensions_requested++;
    }
    (void)generic_recorder_prepare_descriptor(recorder, accepted_tail);
    if ((recorder->state == GENERIC_RECORDER_DRAINING)
        && (recorder->committed_tail == accepted_tail)
        && (recorder->assigned_tail == accepted_tail)
        && (generic_recorder_has_descriptors(recorder) == 0U))
    {
        recorder->state = GENERIC_RECORDER_FINALIZABLE;
        recorder->metrics.stop_drain_duration_us = now_us - recorder->stop_started_us;
    }
}

void generic_recorder_abort(generic_recorder_t *recorder)
{
    if (recorder != 0)
    {
        recorder->state = GENERIC_RECORDER_ABORTED;
    }
}

static uint8_t generic_recorder_write_peek(void *context,
                                           sd_scheduler_candidate_t *candidate)
{
    generic_recorder_t *const recorder = context;
    generic_recorder_write_descriptor_t *const descriptor =
        generic_recorder_ready_descriptor(recorder);
    if ((descriptor == 0) || (candidate == 0)
        || ((recorder->state != GENERIC_RECORDER_CAPTURING)
            && (recorder->state != GENERIC_RECORDER_DRAINING)))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_WRITE;
    candidate->ready = 1U;
    const uint64_t margin = generic_recorder_ring_margin_us(recorder);
    candidate->margin_us = (margin > UINT32_MAX) ? UINT32_MAX : (uint32_t)margin;
    candidate->lba = descriptor->lba
                     + (descriptor->sent_dma_bytes / GENERIC_RECORDER_SECTOR_BYTES);
    candidate->sector_count =
        (descriptor->dma_bytes - descriptor->sent_dma_bytes)
        / GENERIC_RECORDER_SECTOR_BYTES;
    candidate->write_buffer = descriptor->buffer + descriptor->sent_dma_bytes;
    candidate->owner_generation = recorder->generation;
    candidate->media_epoch = descriptor->media_epoch;
    const uint64_t cost =
        (uint64_t)candidate->sector_count
        * recorder->config.estimated_write_us_per_sector;
    candidate->estimated_cost_us =
        (cost > UINT32_MAX) ? UINT32_MAX : (uint32_t)cost;
    return 1U;
}

static sd_scheduler_start_result_t generic_recorder_write_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    generic_recorder_t *const recorder = context;
    generic_recorder_write_descriptor_t *const descriptor =
        generic_recorder_ready_descriptor(recorder);
    if ((descriptor == 0) || (candidate == 0) || (granted_sector_count == 0U)
        || (candidate->owner_generation != recorder->generation)
        || (candidate->media_epoch != recorder->media_epoch)
        || (candidate->lba
            != descriptor->lba
                   + descriptor->sent_dma_bytes / GENERIC_RECORDER_SECTOR_BYTES)
        || (granted_sector_count
            > (descriptor->dma_bytes - descriptor->sent_dma_bytes)
                  / GENERIC_RECORDER_SECTOR_BYTES))
    {
        recorder->error = GENERIC_RECORDER_ERROR_INVALID_ARGUMENT;
        recorder->state = GENERIC_RECORDER_ERROR;
        return SD_SCHEDULER_START_ERROR;
    }
    const uint32_t dma_bytes = granted_sector_count * GENERIC_RECORDER_SECTOR_BYTES;
    const uint32_t remaining_valid = descriptor->valid_bytes
                                     - descriptor->sent_valid_bytes;
    descriptor->active_dma_bytes = dma_bytes;
    descriptor->active_valid_bytes =
        (remaining_valid < dma_bytes) ? remaining_valid : dma_bytes;
    const generic_recorder_transport_start_t result = recorder->config.transport.start(
        recorder->config.transport.context,
        candidate->lba,
        granted_sector_count,
        candidate->write_buffer,
        recorder->generation,
        recorder->media_epoch);
    if (result == GENERIC_RECORDER_TRANSPORT_BUSY)
    {
        descriptor->active_dma_bytes = 0U;
        descriptor->active_valid_bytes = 0U;
        return SD_SCHEDULER_START_BUSY;
    }
    if (result != GENERIC_RECORDER_TRANSPORT_STARTED)
    {
        descriptor->state = GENERIC_RECORDER_DESCRIPTOR_FAILED;
        recorder->metrics.writes_failed++;
        recorder->error = GENERIC_RECORDER_ERROR_TRANSPORT;
        recorder->state = GENERIC_RECORDER_ERROR;
        return SD_SCHEDULER_START_ERROR;
    }
    descriptor->state = GENERIC_RECORDER_DESCRIPTOR_IN_FLIGHT;
    recorder->metrics.writes_submitted++;
    recorder->metrics.write_bytes_submitted += dma_bytes;
    if (dma_bytes > recorder->metrics.max_write_bytes)
    {
        recorder->metrics.max_write_bytes = dma_bytes;
    }
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t generic_recorder_write_poll(void *context)
{
    generic_recorder_t *const recorder = context;
    generic_recorder_write_descriptor_t *const descriptor =
        generic_recorder_in_flight_descriptor(recorder);
    if (descriptor == 0)
    {
        recorder->error = GENERIC_RECORDER_ERROR_INVALID_STATE;
        recorder->state = GENERIC_RECORDER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    generic_recorder_transport_completion_t completion;
    memset(&completion, 0, sizeof(completion));
    const generic_recorder_transport_poll_t result =
        recorder->config.transport.poll(
            recorder->config.transport.context, &completion);
    if (result == GENERIC_RECORDER_TRANSPORT_ACTIVE)
    {
        return SD_SCHEDULER_POLL_ACTIVE;
    }
    if (result == GENERIC_RECORDER_TRANSPORT_RECOVERY_ABORT)
    {
        return SD_SCHEDULER_POLL_RECOVERY_ABORT;
    }
    if ((result != GENERIC_RECORDER_TRANSPORT_COMPLETED)
        || (completion.owner_generation != recorder->generation)
        || (completion.media_epoch != recorder->media_epoch)
        || (completion.lba
            != descriptor->lba
                   + descriptor->sent_dma_bytes / GENERIC_RECORDER_SECTOR_BYTES)
        || (completion.sector_count
            != descriptor->active_dma_bytes / GENERIC_RECORDER_SECTOR_BYTES)
        || (completion.buffer
            != descriptor->buffer + descriptor->sent_dma_bytes))
    {
        descriptor->state = GENERIC_RECORDER_DESCRIPTOR_FAILED;
        recorder->metrics.writes_failed++;
        recorder->error = (result == GENERIC_RECORDER_TRANSPORT_MEDIA_CHANGED)
                              ? GENERIC_RECORDER_ERROR_MEDIA_CHANGED
                              : GENERIC_RECORDER_ERROR_WRITE;
        recorder->state = GENERIC_RECORDER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    const uint64_t expected = descriptor->logical_offset
                              + descriptor->sent_valid_bytes;
    if (recorder->committed_tail != expected)
    {
        descriptor->state = GENERIC_RECORDER_DESCRIPTOR_FAILED;
        recorder->metrics.writes_failed++;
        recorder->error = GENERIC_RECORDER_ERROR_WRITE;
        recorder->state = GENERIC_RECORDER_ERROR;
        return SD_SCHEDULER_POLL_ERROR;
    }
    descriptor->sent_dma_bytes += descriptor->active_dma_bytes;
    descriptor->sent_valid_bytes += descriptor->active_valid_bytes;
    generic_recorder_critical_enter(recorder);
    recorder->committed_tail += descriptor->active_valid_bytes;
    generic_recorder_critical_exit(recorder);
    recorder->metrics.bytes_committed = recorder->committed_tail;
    recorder->metrics.writes_completed++;
    descriptor->active_dma_bytes = 0U;
    descriptor->active_valid_bytes = 0U;
    if (descriptor->sent_dma_bytes == descriptor->dma_bytes)
    {
        if (descriptor->sent_valid_bytes != descriptor->valid_bytes)
        {
            descriptor->state = GENERIC_RECORDER_DESCRIPTOR_FAILED;
            recorder->metrics.writes_failed++;
            recorder->error = GENERIC_RECORDER_ERROR_WRITE;
            recorder->state = GENERIC_RECORDER_ERROR;
            return SD_SCHEDULER_POLL_ERROR;
        }
        memset(descriptor, 0, sizeof(*descriptor));
    }
    else
    {
        descriptor->state = GENERIC_RECORDER_DESCRIPTOR_READY;
    }
    return SD_SCHEDULER_POLL_COMPLETED;
}

static uint8_t generic_recorder_filesystem_peek(
    void *context,
    sd_scheduler_candidate_t *candidate)
{
    generic_recorder_t *const recorder = context;
    if ((candidate == 0) || (recorder->extension_pending == 0U)
        || (recorder->state != GENERIC_RECORDER_CAPTURING))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_FILESYSTEM;
    candidate->ready = 1U;
    const uint64_t margin = generic_recorder_reservation_margin_us(recorder);
    candidate->margin_us = (margin > UINT32_MAX) ? UINT32_MAX : (uint32_t)margin;
    candidate->estimated_cost_us = 100000U;
    candidate->owner_generation = recorder->generation;
    candidate->media_epoch = recorder->media_epoch;
    candidate->reservation =
        (margin <= recorder->config.reservation_critical_margin_us)
            ? SD_SCHEDULER_RESERVATION_CRITICAL
            : SD_SCHEDULER_RESERVATION_LOW;
    return 1U;
}

static sd_scheduler_start_result_t generic_recorder_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    generic_recorder_t *const recorder = context;
    (void)granted_sector_count;
    if ((candidate == 0) || (recorder->extension_pending == 0U)
        || (candidate->owner_generation != recorder->generation)
        || (candidate->media_epoch != recorder->media_epoch))
    {
        return SD_SCHEDULER_START_ERROR;
    }
    const recorder_file_reservation_result_t result =
        recorder->config.reservation.extend(
            recorder->config.reservation.context,
            recorder->config.extension_bytes);
    if (result == RECORDER_FILE_RESERVATION_SD_BUSY)
    {
        return SD_SCHEDULER_START_BUSY;
    }
    if ((result != RECORDER_FILE_RESERVATION_OK)
        && (result != RECORDER_FILE_RESERVATION_PARTIAL))
    {
        recorder->metrics.extensions_failed++;
        recorder->error = (result == RECORDER_FILE_RESERVATION_NO_SPACE)
                              ? GENERIC_RECORDER_ERROR_NO_SPACE
                              : GENERIC_RECORDER_ERROR_RESERVATION;
        recorder->state = (result == RECORDER_FILE_RESERVATION_NO_SPACE)
                              ? GENERIC_RECORDER_DRAINING
                              : GENERIC_RECORDER_ERROR;
        recorder->extension_pending = 0U;
        return SD_SCHEDULER_START_ERROR;
    }
    recorder_file_reservation_map_snapshot_t snapshot;
    if (generic_recorder_snapshot(recorder, &snapshot) == 0U)
    {
        recorder->metrics.extensions_failed++;
        return SD_SCHEDULER_START_ERROR;
    }
    recorder->extension_pending = 0U;
    recorder->metrics.extensions_completed++;
    return SD_SCHEDULER_START_COMPLETED;
}

sd_scheduler_provider_t generic_recorder_write_provider(generic_recorder_t *recorder)
{
    const sd_scheduler_provider_t provider = {
        .context = recorder,
        .peek = generic_recorder_write_peek,
        .start = generic_recorder_write_start,
        .poll = generic_recorder_write_poll,
    };
    return provider;
}

sd_scheduler_provider_t generic_recorder_filesystem_provider(
    generic_recorder_t *recorder)
{
    const sd_scheduler_provider_t provider = {
        .context = recorder,
        .peek = generic_recorder_filesystem_peek,
        .start = generic_recorder_filesystem_start,
        .poll = 0,
    };
    return provider;
}
