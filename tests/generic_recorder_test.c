#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "SD/sd_scheduler.h"
#include "Storage/generic_recorder.h"

#define TEST_SECTOR_BYTES 512U
#define TEST_DISK_SECTORS 20000U
#define TEST_RING_FRAMES 12001U
#define TEST_WRITE_BYTES (32U * 1024U)
#define TEST_MAX_EXPECTED_BYTES (4U * 1024U * 1024U)
#define TEST_BLOCK_MAX_FRAMES 960U

typedef struct
{
    sample_stream_physical_extent_t extents[RECORDER_FILE_RESERVATION_MAX_EXTENTS];
    uint32_t file_sectors;
    uint32_t next_lba;
    uint32_t generation;
    uint32_t media_epoch;
    uint16_t extent_count;
    uint8_t no_space;
    uint8_t invalid_map;
} fake_reservation_t;

typedef struct
{
    uint32_t lba;
    uint32_t sectors;
    const uint8_t *buffer;
    uint32_t generation;
    uint32_t epoch;
    uint32_t polls_left;
    uint32_t loops_while_active;
    uint32_t producer_blocks_while_active;
    uint8_t active;
    uint8_t fail_next;
} fake_transport_t;

typedef struct
{
    uint32_t starts;
    uint32_t polls_left;
    uint8_t active;
    uint8_t ready;
    uint8_t buffer[4096];
} fake_read_t;

static uint8_t g_disk[TEST_DISK_SECTORS * TEST_SECTOR_BYTES];
static int32_t g_ring[TEST_RING_FRAMES * 2U];
static _Alignas(32) uint8_t g_write_a[TEST_WRITE_BYTES];
static _Alignas(32) uint8_t g_write_b[TEST_WRITE_BYTES];
static int32_t g_input[TEST_BLOCK_MAX_FRAMES * 2U];
static uint8_t g_expected[TEST_MAX_EXPECTED_BYTES];

static void fake_reservation_add(fake_reservation_t *reservation,
                                 uint32_t sectors)
{
    while (sectors != 0U)
    {
        assert(reservation->extent_count < RECORDER_FILE_RESERVATION_MAX_EXTENTS);
        uint32_t chunk = 97U + ((reservation->extent_count % 3U) * 31U);
        if (chunk > sectors)
        {
            chunk = sectors;
        }
        sample_stream_physical_extent_t *const extent =
            &reservation->extents[reservation->extent_count++];
        extent->file_sector_start = reservation->file_sectors;
        extent->lba_start = reservation->next_lba;
        extent->sector_count = chunk;
        reservation->file_sectors += chunk;
        reservation->next_lba += chunk + 13U;
        assert(reservation->next_lba < TEST_DISK_SECTORS);
        sectors -= chunk;
    }
    reservation->generation++;
}

static void fake_reservation_init(fake_reservation_t *reservation,
                                  uint32_t data_bytes)
{
    memset(reservation, 0, sizeof(*reservation));
    reservation->next_lba = 100U;
    reservation->generation = 1U;
    reservation->media_epoch = 9U;
    const uint32_t total_bytes = TEST_SECTOR_BYTES + data_bytes;
    fake_reservation_add(
        reservation, (total_bytes + TEST_SECTOR_BYTES - 1U) / TEST_SECTOR_BYTES);
    memset(g_disk, 0xA5, sizeof(g_disk));
}

static uint8_t fake_snapshot(
    void *context,
    recorder_file_reservation_map_snapshot_t *snapshot)
{
    fake_reservation_t *const reservation = context;
    snapshot->extents = reservation->extents;
    snapshot->reserved_file_bytes =
        (uint64_t)reservation->file_sectors * TEST_SECTOR_BYTES;
    snapshot->valid_file_bytes = 0U;
    snapshot->generation = reservation->generation;
    snapshot->media_epoch = reservation->media_epoch;
    snapshot->extent_count = reservation->extent_count;
    snapshot->sector_size = TEST_SECTOR_BYTES;
    return 1U;
}

static uint8_t fake_resolve(
    void *context,
    const recorder_file_reservation_map_snapshot_t *snapshot,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_span_t *span)
{
    fake_reservation_t *const reservation = context;
    if ((reservation->invalid_map != 0U) || (requested_bytes == 0U)
        || (file_byte_offset >= snapshot->reserved_file_bytes))
    {
        return 0U;
    }
    const uint32_t file_sector = (uint32_t)(file_byte_offset / TEST_SECTOR_BYTES);
    const uint16_t skip = (uint16_t)(file_byte_offset % TEST_SECTOR_BYTES);
    for (uint16_t i = 0U; i < snapshot->extent_count; ++i)
    {
        const sample_stream_physical_extent_t *const extent =
            &snapshot->extents[i];
        if ((file_sector < extent->file_sector_start)
            || (file_sector >= extent->file_sector_start + extent->sector_count))
        {
            continue;
        }
        const uint32_t in_extent = file_sector - extent->file_sector_start;
        uint32_t available =
            (extent->sector_count - in_extent) * TEST_SECTOR_BYTES - skip;
        if (available > requested_bytes)
        {
            available = requested_bytes;
        }
        span->lba = extent->lba_start + in_extent;
        span->logical_bytes = available;
        span->first_sector_skip = skip;
        span->sector_count =
            (skip + available + TEST_SECTOR_BYTES - 1U) / TEST_SECTOR_BYTES;
        span->extent_index = i;
        return 1U;
    }
    return 0U;
}

static recorder_file_reservation_result_t fake_extend(
    void *context,
    uint64_t additional_bytes)
{
    fake_reservation_t *const reservation = context;
    if (reservation->no_space != 0U)
    {
        return RECORDER_FILE_RESERVATION_NO_SPACE;
    }
    const uint32_t sectors =
        (uint32_t)((additional_bytes + TEST_SECTOR_BYTES - 1U)
                   / TEST_SECTOR_BYTES);
    fake_reservation_add(reservation, sectors);
    return RECORDER_FILE_RESERVATION_OK;
}

static generic_recorder_transport_start_t fake_transport_start(
    void *context,
    uint32_t lba,
    uint32_t sector_count,
    const void *buffer,
    uint32_t generation,
    uint32_t epoch)
{
    fake_transport_t *const transport = context;
    if (transport->active != 0U)
    {
        return GENERIC_RECORDER_TRANSPORT_BUSY;
    }
    transport->lba = lba;
    transport->sectors = sector_count;
    transport->buffer = buffer;
    transport->generation = generation;
    transport->epoch = epoch;
    transport->polls_left = 3U;
    transport->active = 1U;
    return GENERIC_RECORDER_TRANSPORT_STARTED;
}

static generic_recorder_transport_poll_t fake_transport_poll(
    void *context,
    generic_recorder_transport_completion_t *completion)
{
    fake_transport_t *const transport = context;
    assert(transport->active != 0U);
    transport->loops_while_active++;
    if (transport->polls_left-- != 0U)
    {
        return GENERIC_RECORDER_TRANSPORT_ACTIVE;
    }
    completion->lba = transport->lba;
    completion->sector_count = transport->sectors;
    completion->buffer = transport->buffer;
    completion->owner_generation = transport->generation;
    completion->media_epoch = transport->epoch;
    transport->active = 0U;
    if (transport->fail_next != 0U)
    {
        transport->fail_next = 0U;
        return GENERIC_RECORDER_TRANSPORT_FAILED;
    }
    assert((transport->lba + transport->sectors) <= TEST_DISK_SECTORS);
    memcpy(&g_disk[transport->lba * TEST_SECTOR_BYTES],
           transport->buffer,
           transport->sectors * TEST_SECTOR_BYTES);
    return GENERIC_RECORDER_TRANSPORT_COMPLETED;
}

static uint8_t fake_read_peek(void *context, sd_scheduler_candidate_t *candidate)
{
    fake_read_t *const read = context;
    if ((read->ready == 0U) || (read->active != 0U))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = SD_SCHEDULER_CLASS_READ;
    candidate->ready = 1U;
    candidate->margin_us = 500000U;
    candidate->estimated_cost_us = 1500U;
    candidate->lba = 10U;
    candidate->sector_count = 8U;
    candidate->read_buffer = read->buffer;
    candidate->media_epoch = 9U;
    return 1U;
}

static sd_scheduler_start_result_t fake_read_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    fake_read_t *const read = context;
    (void)candidate;
    assert(granted_sector_count == 8U);
    read->starts++;
    read->polls_left = 1U;
    read->active = 1U;
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t fake_read_poll(void *context)
{
    fake_read_t *const read = context;
    if (read->polls_left-- != 0U)
    {
        return SD_SCHEDULER_POLL_ACTIVE;
    }
    read->active = 0U;
    return SD_SCHEDULER_POLL_COMPLETED;
}

static uint8_t logical_read_byte(const fake_reservation_t *reservation,
                                 uint64_t file_offset)
{
    const uint32_t file_sector = (uint32_t)(file_offset / TEST_SECTOR_BYTES);
    const uint32_t in_sector = (uint32_t)(file_offset % TEST_SECTOR_BYTES);
    for (uint16_t i = 0U; i < reservation->extent_count; ++i)
    {
        const sample_stream_physical_extent_t *const extent =
            &reservation->extents[i];
        if ((file_sector >= extent->file_sector_start)
            && (file_sector < extent->file_sector_start + extent->sector_count))
        {
            const uint32_t lba = extent->lba_start
                                 + file_sector - extent->file_sector_start;
            return g_disk[lba * TEST_SECTOR_BYTES + in_sector];
        }
    }
    assert(0);
    return 0U;
}

static int32_t pattern_sample(uint32_t pattern,
                              uint64_t frame,
                              uint32_t channel,
                              uint32_t *random_state)
{
    if (pattern == 0U)
    {
        return 0;
    }
    if (pattern == 1U)
    {
        return (((frame + channel) & 1U) != 0U) ? 8388607 : -8388608;
    }
    if (pattern == 2U)
    {
        return (int32_t)(((frame * 2U + channel) & 0xFFFFFFU) - 0x800000U);
    }
    *random_state ^= *random_state << 13;
    *random_state ^= *random_state >> 17;
    *random_state ^= *random_state << 5;
    return (int32_t)((*random_state & 0xFFFFFFU) - 0x800000U);
}

typedef struct
{
    generic_recorder_t recorder;
    fake_reservation_t reservation;
    fake_transport_t transport;
    fake_read_t read;
    sd_scheduler_t scheduler;
    uint32_t now_us;
    uint64_t previous_accepted;
    uint64_t previous_assigned;
    uint64_t previous_committed;
} test_fixture_t;

static void fixture_init(test_fixture_t *fixture, uint32_t initial_data_bytes)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(g_ring, 0, sizeof(g_ring));
    memset(g_write_a, 0, sizeof(g_write_a));
    memset(g_write_b, 0, sizeof(g_write_b));
    fake_reservation_init(&fixture->reservation, initial_data_bytes);

    generic_recorder_config_t config;
    memset(&config, 0, sizeof(config));
    config.ring_interleaved = g_ring;
    config.ring_capacity_frames = TEST_RING_FRAMES;
    config.write_buffers[0] = g_write_a;
    config.write_buffers[1] = g_write_b;
    config.write_buffer_bytes = TEST_WRITE_BYTES;
    config.minimum_write_bytes = 8U * 1024U;
    config.sample_rate_hz = 48000U;
    config.channels = 2U;
    config.reserved_header_bytes = TEST_SECTOR_BYTES;
    config.extension_bytes = 576000U;
    config.reservation_low_margin_us = 2000000U;
    config.reservation_critical_margin_us = 500000U;
    config.estimated_write_us_per_sector = 100U;
    config.transport.context = &fixture->transport;
    config.transport.start = fake_transport_start;
    config.transport.poll = fake_transport_poll;
    config.reservation.context = &fixture->reservation;
    config.reservation.snapshot = fake_snapshot;
    config.reservation.resolve = fake_resolve;
    config.reservation.extend = fake_extend;
    assert(generic_recorder_begin(&fixture->recorder, &config) != 0U);

    sd_scheduler_config_t scheduler_config;
    sd_scheduler_default_config(&scheduler_config);
    scheduler_config.worst_case_us_per_sector = 100U;
    scheduler_config.max_write_sectors = 64U;
    sd_scheduler_init(&fixture->scheduler, &scheduler_config);
    fixture->read.ready = 1U;
    const sd_scheduler_provider_t read_provider = {
        .context = &fixture->read,
        .peek = fake_read_peek,
        .start = fake_read_start,
        .poll = fake_read_poll,
    };
    sd_scheduler_provider_t write_provider =
        generic_recorder_write_provider(&fixture->recorder);
    sd_scheduler_provider_t filesystem_provider =
        generic_recorder_filesystem_provider(&fixture->recorder);
    assert(sd_scheduler_bind_provider(
               &fixture->scheduler, SD_SCHEDULER_CLASS_READ, &read_provider)
           != 0U);
    assert(sd_scheduler_bind_provider(
               &fixture->scheduler, SD_SCHEDULER_CLASS_WRITE, &write_provider)
           != 0U);
    assert(sd_scheduler_bind_provider(&fixture->scheduler,
                                      SD_SCHEDULER_CLASS_FILESYSTEM,
                                      &filesystem_provider)
           != 0U);
}

static void fixture_step(test_fixture_t *fixture, uint8_t producer_active)
{
    if ((producer_active != 0U) && (fixture->transport.active != 0U))
    {
        fixture->transport.producer_blocks_while_active++;
    }
    generic_recorder_service(&fixture->recorder, fixture->now_us);
    sd_scheduler_service(&fixture->scheduler,
                         fixture->now_us,
                         fixture->reservation.media_epoch);
    assert(generic_recorder_invariants_hold(&fixture->recorder) != 0U);
    assert(fixture->recorder.accepted_tail >= fixture->previous_accepted);
    assert(fixture->recorder.assigned_tail >= fixture->previous_assigned);
    assert(fixture->recorder.committed_tail >= fixture->previous_committed);
    fixture->previous_accepted = fixture->recorder.accepted_tail;
    fixture->previous_assigned = fixture->recorder.assigned_tail;
    fixture->previous_committed = fixture->recorder.committed_tail;
    fixture->now_us += 1000U;
}

static void append_expected(const int32_t *samples,
                            uint32_t frames,
                            uint64_t *expected_bytes)
{
    for (uint32_t frame = 0U; frame < frames; ++frame)
    {
        for (uint32_t channel = 0U; channel < 2U; ++channel)
        {
            const uint32_t value = (uint32_t)samples[frame * 2U + channel];
            assert((*expected_bytes + 3U) <= TEST_MAX_EXPECTED_BYTES);
            g_expected[(*expected_bytes)++] = (uint8_t)value;
            g_expected[(*expected_bytes)++] = (uint8_t)(value >> 8);
            g_expected[(*expected_bytes)++] = (uint8_t)(value >> 16);
        }
    }
}

static void verify_bit_perfect(const test_fixture_t *fixture,
                               uint64_t expected_bytes)
{
    for (uint64_t i = 0U; i < expected_bytes; ++i)
    {
        assert(logical_read_byte(
                   &fixture->reservation, TEST_SECTOR_BYTES + i)
               == g_expected[i]);
    }
    const uint32_t padding = (uint32_t)(
        (TEST_SECTOR_BYTES - (expected_bytes % TEST_SECTOR_BYTES))
        % TEST_SECTOR_BYTES);
    for (uint32_t i = 0U; i < padding; ++i)
    {
        assert(logical_read_byte(&fixture->reservation,
                                 TEST_SECTOR_BYTES + expected_bytes + i)
               == 0U);
    }
}

static void run_recording(uint64_t target_frames,
                          uint32_t pattern,
                          uint8_t variable_profile,
                          uint8_t require_extensions)
{
    test_fixture_t fixture;
    fixture_init(&fixture, 5U * 288000U);
    uint64_t produced_frames = 0U;
    uint64_t expected_bytes = 0U;
    uint32_t random_state = 0x13579BDFU;
    uint32_t block_index = 0U;
    while (produced_frames < target_frames)
    {
        uint32_t frames = 480U;
        if (variable_profile != 0U)
        {
            const uint32_t phase = block_index % 20U;
            frames = (phase < 4U) ? 240U
                     : (phase < 8U) ? 720U
                     : (phase < 10U) ? 960U
                     : (phase == 10U) ? 0U
                                      : 480U;
        }
        if (frames > (target_frames - produced_frames))
        {
            frames = (uint32_t)(target_frames - produced_frames);
        }
        if (frames != 0U)
        {
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                for (uint32_t channel = 0U; channel < 2U; ++channel)
                {
                    g_input[frame * 2U + channel] = pattern_sample(
                        pattern, produced_frames + frame, channel, &random_state);
                }
            }
            if (generic_recorder_push(&fixture.recorder, g_input, frames) == 0U)
            {
                fprintf(stderr,
                        "push failed target=%llu produced=%llu state=%u error=%u accepted=%llu committed=%llu reserved=%llu\n",
                        (unsigned long long)target_frames,
                        (unsigned long long)produced_frames,
                        (unsigned)fixture.recorder.state,
                        (unsigned)fixture.recorder.error,
                        (unsigned long long)fixture.recorder.accepted_tail,
                        (unsigned long long)fixture.recorder.committed_tail,
                        (unsigned long long)fixture.recorder.reserved_capacity);
                assert(0);
            }
            append_expected(g_input, frames, &expected_bytes);
            produced_frames += frames;
        }
        for (uint32_t service = 0U; service < 10U; ++service)
        {
            fixture_step(&fixture, (frames != 0U) ? 1U : 0U);
        }
        block_index++;
    }
    assert(generic_recorder_request_stop(&fixture.recorder, fixture.now_us) != 0U);
    for (uint32_t guard = 0U;
         (guard < 20000U)
         && (fixture.recorder.state != GENERIC_RECORDER_FINALIZABLE);
         ++guard)
    {
        fixture_step(&fixture, 0U);
    }
    assert(fixture.recorder.state == GENERIC_RECORDER_FINALIZABLE);
    assert(fixture.recorder.accepted_tail == expected_bytes);
    assert(fixture.recorder.assigned_tail == expected_bytes);
    assert(fixture.recorder.committed_tail == expected_bytes);
    assert(fixture.recorder.metrics.frames_accepted == target_frames);
    assert(fixture.recorder.metrics.bytes_packed == expected_bytes);
    assert(fixture.recorder.metrics.writes_submitted != 0U);
    assert(fixture.recorder.metrics.writes_completed != 0U);
    assert(fixture.transport.loops_while_active != 0U);
    generic_recorder_status_t status;
    generic_recorder_metrics_t recorder_metrics;
    generic_recorder_get_status(&fixture.recorder, &status);
    generic_recorder_get_metrics(&fixture.recorder, &recorder_metrics);
    assert(status.fully_committed != 0U);
    assert(status.accepting == 0U);
    assert(recorder_metrics.bytes_committed == expected_bytes);
    if (target_frames > 48000U)
    {
        assert(fixture.transport.producer_blocks_while_active != 0U);
        assert(fixture.read.starts != 0U);
    }
    if (require_extensions != 0U)
    {
        assert(fixture.recorder.metrics.extensions_completed >= 2U);
        assert(fixture.recorder.metrics.extent_crossings >= 2U);
        sd_scheduler_metrics_t scheduler_metrics;
        sd_scheduler_metrics_get(&fixture.scheduler, &scheduler_metrics);
        assert(scheduler_metrics.read_transactions != 0U);
        assert(scheduler_metrics.write_transactions != 0U);
        assert(scheduler_metrics.filesystem_slots >= 2U);
        printf("long frames=%llu bytes=%llu extensions=%lu extents=%lu writes=%lu read=%lu fs=%lu dma_loops=%lu ring_high=%lu ring_min_free=%lu reservation_min=%llu backlog_max=%llu drain_us=%lu\n",
               (unsigned long long)target_frames,
               (unsigned long long)expected_bytes,
               (unsigned long)fixture.recorder.metrics.extensions_completed,
               (unsigned long)fixture.recorder.metrics.extent_crossings,
               (unsigned long)fixture.recorder.metrics.writes_completed,
               (unsigned long)scheduler_metrics.read_transactions,
               (unsigned long)scheduler_metrics.filesystem_slots,
               (unsigned long)fixture.transport.loops_while_active,
               (unsigned long)fixture.recorder.metrics.ring_high_watermark_frames,
               (unsigned long)fixture.recorder.metrics.ring_min_free_frames,
               (unsigned long long)fixture.recorder.metrics.reservation_min_margin_bytes,
               (unsigned long long)fixture.recorder.metrics.max_backlog_bytes,
               (unsigned long)fixture.recorder.metrics.stop_drain_duration_us);
    }
    verify_bit_perfect(&fixture, expected_bytes);
    for (uint32_t i = 0U; i < TEST_SECTOR_BYTES; ++i)
    {
        assert(logical_read_byte(&fixture.reservation, i) == 0xA5U);
    }
}

static void test_lengths_patterns_and_extensions(void)
{
    run_recording(37U, 0U, 0U, 0U);
    run_recording(2048U, 1U, 0U, 0U);
    run_recording(48000U, 2U, 0U, 0U);
    run_recording(3U * 48000U + 13U, 3U, 1U, 0U);
    run_recording(12U * 48000U + 37U, 3U, 1U, 1U);
}

static void test_write_failure_does_not_commit(void)
{
    test_fixture_t fixture;
    fixture_init(&fixture, 5U * 288000U);
    memset(g_input, 0x35, sizeof(g_input));
    assert(generic_recorder_push(
               &fixture.recorder, g_input, TEST_BLOCK_MAX_FRAMES)
           != 0U);
    assert(generic_recorder_push(
               &fixture.recorder, g_input, TEST_BLOCK_MAX_FRAMES)
           != 0U);
    fixture.transport.fail_next = 1U;
    for (uint32_t i = 0U;
         (i < 100U) && (fixture.recorder.state != GENERIC_RECORDER_ERROR);
         ++i)
    {
        fixture_step(&fixture, 0U);
    }
    assert(fixture.recorder.state == GENERIC_RECORDER_ERROR);
    assert(fixture.recorder.committed_tail == 0U);
    assert(fixture.recorder.metrics.writes_failed == 1U);
}

static void test_media_mapping_and_ring_errors(void)
{
    test_fixture_t media;
    fixture_init(&media, 5U * 288000U);
    media.reservation.media_epoch++;
    generic_recorder_service(&media.recorder, 0U);
    assert(media.recorder.state == GENERIC_RECORDER_ERROR);
    assert(media.recorder.error == GENERIC_RECORDER_ERROR_MEDIA_CHANGED);
    assert(media.recorder.committed_tail == 0U);

    test_fixture_t mapping;
    fixture_init(&mapping, 5U * 288000U);
    assert(generic_recorder_push(
               &mapping.recorder, g_input, TEST_BLOCK_MAX_FRAMES)
           != 0U);
    assert(generic_recorder_push(
               &mapping.recorder, g_input, TEST_BLOCK_MAX_FRAMES)
           != 0U);
    mapping.reservation.invalid_map = 1U;
    generic_recorder_service(&mapping.recorder, 0U);
    assert(mapping.recorder.state == GENERIC_RECORDER_ERROR);
    assert(mapping.recorder.committed_tail == 0U);

    test_fixture_t ring;
    fixture_init(&ring, 5U * 288000U);
    for (uint32_t i = 0U; i < 26U; ++i)
    {
        const uint8_t accepted =
            generic_recorder_push(&ring.recorder, g_input, 480U);
        if (i < 25U)
        {
            assert(accepted != 0U);
        }
        else
        {
            assert(accepted == 0U);
        }
    }
    assert(ring.recorder.state == GENERIC_RECORDER_DRAINING);
    assert(ring.recorder.error == GENERIC_RECORDER_ERROR_RING_FULL);
    assert(ring.recorder.committed_tail == 0U);

    test_fixture_t no_space;
    fixture_init(&no_space, 100000U);
    no_space.reservation.no_space = 1U;
    assert(generic_recorder_push(&no_space.recorder, g_input, 480U) != 0U);
    for (uint32_t i = 0U;
         (i < 20U)
         && (no_space.recorder.error != GENERIC_RECORDER_ERROR_NO_SPACE);
         ++i)
    {
        fixture_step(&no_space, 0U);
    }
    assert(no_space.recorder.error == GENERIC_RECORDER_ERROR_NO_SPACE);
    assert(no_space.recorder.state == GENERIC_RECORDER_DRAINING);
    assert(no_space.recorder.committed_tail == 0U);
}

int main(void)
{
    test_lengths_patterns_and_extensions();
    test_write_failure_does_not_commit();
    test_media_mapping_and_ring_errors();
    puts("generic_recorder_test: PASS (PCM24 bit-perfect, multi-extents, extensions, READ/WRITE/FS, STOP drain)");
    return 0;
}
