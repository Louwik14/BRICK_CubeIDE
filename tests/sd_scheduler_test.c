#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "SD/sd_scheduler.h"

typedef struct
{
    sd_scheduler_candidate_t candidate;
    uint32_t starts;
    uint32_t completions;
    uint32_t last_grant;
    uint32_t active_polls;
    uint32_t polls_to_complete;
    sd_scheduler_poll_result_t completion_result;
    sd_scheduler_start_result_t start_result;
    uint8_t persistent;
    uint8_t active;
} fake_provider_t;

typedef struct
{
    sd_scheduler_t scheduler;
    fake_provider_t read;
    fake_provider_t write;
    fake_provider_t filesystem;
    uint8_t read_buffer[64U * SD_SCHEDULER_SECTOR_BYTES];
    uint8_t write_buffer[64U * SD_SCHEDULER_SECTOR_BYTES];
    uint32_t now_us;
    uint32_t epoch;
} fixture_t;

typedef struct
{
    uint8_t buffer[64U * SD_SCHEDULER_SECTOR_BYTES];
    uint32_t capacity_bytes;
    uint32_t fill_bytes;
    uint32_t bytes_per_second;
    uint32_t starts;
    uint32_t written_bytes;
    uint32_t epoch;
    uint8_t active;
} synthetic_write_ring_t;

static uint8_t fake_peek(void *context, sd_scheduler_candidate_t *candidate)
{
    fake_provider_t *const fake = context;
    *candidate = fake->candidate;
    return fake->candidate.ready;
}

static sd_scheduler_start_result_t fake_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    fake_provider_t *const fake = context;
    assert(fake->active == 0U);
    assert(candidate->type == fake->candidate.type);
    fake->starts++;
    fake->last_grant = granted_sector_count;
    if (fake->persistent == 0U)
    {
        fake->candidate.ready = 0U;
    }
    if (fake->start_result != SD_SCHEDULER_START_STARTED)
    {
        return fake->start_result;
    }
    fake->active = 1U;
    fake->active_polls = 0U;
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_start_result_t fake_filesystem_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    fake_provider_t *const fake = context;
    assert(candidate->type == SD_SCHEDULER_CLASS_FILESYSTEM);
    assert(granted_sector_count == 0U);
    fake->starts++;
    fake->completions++;
    if (fake->persistent == 0U)
    {
        fake->candidate.ready = 0U;
    }
    return (fake->start_result == SD_SCHEDULER_START_ERROR)
               ? SD_SCHEDULER_START_ERROR
               : SD_SCHEDULER_START_COMPLETED;
}

static sd_scheduler_poll_result_t fake_poll(void *context)
{
    fake_provider_t *const fake = context;
    assert(fake->active != 0U);
    if (fake->active_polls++ < fake->polls_to_complete)
    {
        return (fake->completion_result == SD_SCHEDULER_POLL_RECOVERY_ABORT)
                   ? SD_SCHEDULER_POLL_RECOVERY_ABORT
                   : SD_SCHEDULER_POLL_ACTIVE;
    }
    fake->active = 0U;
    fake->completions++;
    return fake->completion_result;
}

static void synthetic_write_produce(synthetic_write_ring_t *ring,
                                    uint32_t elapsed_us,
                                    uint32_t rate_percent)
{
    const uint64_t produced =
        ((uint64_t)ring->bytes_per_second * elapsed_us * rate_percent)
        / 100000000ULL;
    const uint64_t filled = (uint64_t)ring->fill_bytes + produced;
    ring->fill_bytes = (filled > ring->capacity_bytes)
                           ? ring->capacity_bytes
                           : (uint32_t)filled;
}

static uint8_t synthetic_write_peek(void *context,
                                    sd_scheduler_candidate_t *candidate)
{
    synthetic_write_ring_t *const ring = context;
    memset(candidate, 0, sizeof(*candidate));
    if ((ring->fill_bytes < (8U * 1024U)) || (ring->bytes_per_second == 0U))
    {
        return 0U;
    }
    const uint32_t free_bytes = ring->capacity_bytes - ring->fill_bytes;
    candidate->type = SD_SCHEDULER_CLASS_WRITE;
    candidate->ready = 1U;
    candidate->margin_us = (uint32_t)(((uint64_t)free_bytes * 1000000ULL)
                                      / ring->bytes_per_second);
    candidate->sector_count = ring->fill_bytes / SD_SCHEDULER_SECTOR_BYTES;
    if (candidate->sector_count > 64U)
    {
        candidate->sector_count = 64U;
    }
    candidate->estimated_cost_us = candidate->sector_count * 100U;
    candidate->lba = 4000U + (ring->written_bytes / SD_SCHEDULER_SECTOR_BYTES);
    candidate->write_buffer = ring->buffer;
    candidate->owner_generation = 3U;
    candidate->media_epoch = ring->epoch;
    return 1U;
}

static sd_scheduler_start_result_t synthetic_write_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    synthetic_write_ring_t *const ring = context;
    const uint32_t bytes = granted_sector_count * SD_SCHEDULER_SECTOR_BYTES;
    assert(candidate->write_buffer == ring->buffer);
    assert(bytes <= ring->fill_bytes);
    ring->fill_bytes -= bytes;
    ring->written_bytes += bytes;
    ring->starts++;
    ring->active = 1U;
    return SD_SCHEDULER_START_STARTED;
}

static sd_scheduler_poll_result_t synthetic_write_poll(void *context)
{
    synthetic_write_ring_t *const ring = context;
    assert(ring->active != 0U);
    ring->active = 0U;
    return SD_SCHEDULER_POLL_COMPLETED;
}

static void bind_fake(sd_scheduler_t *scheduler,
                      sd_scheduler_class_t type,
                      fake_provider_t *fake)
{
    const sd_scheduler_provider_t provider = {
        .context = fake,
        .peek = fake_peek,
        .start = (type == SD_SCHEDULER_CLASS_FILESYSTEM)
                     ? fake_filesystem_start
                     : fake_start,
        .poll = (type == SD_SCHEDULER_CLASS_FILESYSTEM) ? 0 : fake_poll,
    };
    assert(sd_scheduler_bind_provider(scheduler, type, &provider) != 0U);
}

static void fixture_init(fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    sd_scheduler_config_t config;
    sd_scheduler_default_config(&config);
    config.worst_case_us_per_sector = 100U;
    config.max_write_sectors = 64U;
    config.transaction_guard_us = 2000U;
    config.starvation_limit_us = 20000U;
    sd_scheduler_init(&fixture->scheduler, &config);
    fixture->epoch = 7U;

    fixture->read.candidate.type = SD_SCHEDULER_CLASS_READ;
    fixture->read.candidate.margin_us = 100000U;
    fixture->read.candidate.estimated_cost_us = 4000U;
    fixture->read.candidate.lba = 100U;
    fixture->read.candidate.sector_count = 16U;
    fixture->read.candidate.read_buffer = fixture->read_buffer;
    fixture->read.candidate.media_epoch = fixture->epoch;
    fixture->read.completion_result = SD_SCHEDULER_POLL_COMPLETED;

    fixture->write.candidate.type = SD_SCHEDULER_CLASS_WRITE;
    fixture->write.candidate.margin_us = 100000U;
    fixture->write.candidate.estimated_cost_us = 5000U;
    fixture->write.candidate.lba = 1000U;
    fixture->write.candidate.sector_count = 16U;
    fixture->write.candidate.write_buffer = fixture->write_buffer;
    fixture->write.candidate.media_epoch = fixture->epoch;
    fixture->write.completion_result = SD_SCHEDULER_POLL_COMPLETED;

    fixture->filesystem.candidate.type = SD_SCHEDULER_CLASS_FILESYSTEM;
    fixture->filesystem.candidate.margin_us = 5000000U;
    fixture->filesystem.candidate.media_epoch = fixture->epoch;
    fixture->filesystem.candidate.reservation = SD_SCHEDULER_RESERVATION_SAFE;

    bind_fake(&fixture->scheduler, SD_SCHEDULER_CLASS_READ, &fixture->read);
    bind_fake(&fixture->scheduler, SD_SCHEDULER_CLASS_WRITE, &fixture->write);
    bind_fake(&fixture->scheduler,
              SD_SCHEDULER_CLASS_FILESYSTEM,
              &fixture->filesystem);
}

static void service(fixture_t *fixture)
{
    sd_scheduler_service(&fixture->scheduler, fixture->now_us, fixture->epoch);
    fixture->now_us += 1000U;
}

static void finish_active(fixture_t *fixture)
{
    uint32_t guard = 100U;
    while ((sd_scheduler_owner(&fixture->scheduler) != SD_SCHEDULER_OWNER_IDLE)
           && (guard-- != 0U))
    {
        service(fixture);
    }
    assert(sd_scheduler_owner(&fixture->scheduler) == SD_SCHEDULER_OWNER_IDLE);
}

static sd_scheduler_class_t start_one(fixture_t *fixture)
{
    const uint32_t reads = fixture->read.starts;
    const uint32_t writes = fixture->write.starts;
    const uint32_t filesystems = fixture->filesystem.starts;
    service(fixture);
    sd_scheduler_class_t picked = SD_SCHEDULER_CLASS_NONE;
    if (fixture->read.starts != reads)
    {
        picked = SD_SCHEDULER_CLASS_READ;
    }
    else if (fixture->write.starts != writes)
    {
        picked = SD_SCHEDULER_CLASS_WRITE;
    }
    else if (fixture->filesystem.starts != filesystems)
    {
        picked = SD_SCHEDULER_CLASS_FILESYSTEM;
    }
    finish_active(fixture);
    return picked;
}

static void test_a_b_single_class(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.candidate.ready = 1U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    f.write.candidate.ready = 1U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
}

static void test_c_to_f_deadlines(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.persistent = 1U;
    f.write.persistent = 1U;
    f.read.candidate.ready = 1U;
    f.write.candidate.ready = 1U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);

    f.read.candidate.margin_us = 10000U;
    f.write.candidate.margin_us = 100000U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    f.read.candidate.margin_us = 100000U;
    f.write.candidate.margin_us = 10000U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    f.read.candidate.margin_us = 5000U;
    f.write.candidate.margin_us = 8000U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    f.read.candidate.margin_us = 7000U;
    f.write.candidate.margin_us = 7000U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    assert(f.scheduler.metrics.critical_ties == 1U);
}

static void test_g_h_no_starvation(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.persistent = 1U;
    f.write.persistent = 1U;
    f.read.candidate.ready = 1U;
    f.write.candidate.ready = 1U;
    for (uint32_t i = 0U; i < 100U; ++i)
    {
        (void)start_one(&f);
    }
    assert(f.read.starts == 50U);
    assert(f.write.starts == 50U);
    assert(f.scheduler.metrics.max_read_wait_us < 10000U);
    assert(f.scheduler.metrics.max_write_wait_us < 10000U);
}

static void test_i_filesystem_watermarks(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.persistent = 1U;
    f.write.persistent = 1U;
    f.read.candidate.ready = 1U;
    f.write.candidate.ready = 1U;
    f.filesystem.candidate.ready = 1U;
    assert(f.filesystem.candidate.margin_us == 5000000U);
    assert(start_one(&f) != SD_SCHEDULER_CLASS_NONE);

    f.filesystem.candidate.ready = 1U;
    f.filesystem.candidate.margin_us = 2000000U;
    f.filesystem.candidate.reservation = SD_SCHEDULER_RESERVATION_LOW;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_FILESYSTEM);
    assert(f.scheduler.metrics.min_reservation_margin_us == 2000000U);

    f.filesystem.candidate.ready = 1U;
    f.filesystem.candidate.margin_us = 500000U;
    f.filesystem.candidate.reservation = SD_SCHEDULER_RESERVATION_CRITICAL;
    f.read.candidate.margin_us = 10000U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    assert(f.scheduler.metrics.reservation_policy_failures != 0U);
}

static void test_j_switches_and_bursts(void)
{
    fixture_t read_fixture;
    fixture_init(&read_fixture);
    const uint32_t read_sectors[] = {8U, 16U, 32U, 64U};
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        read_fixture.read.candidate.sector_count = read_sectors[i];
        read_fixture.read.candidate.ready = 1U;
        assert(start_one(&read_fixture) == SD_SCHEDULER_CLASS_READ);
        assert(read_fixture.read.last_grant == read_sectors[i]);
    }

    fixture_t f;
    fixture_init(&f);
    f.read.persistent = 1U;
    f.write.persistent = 1U;
    f.read.candidate.ready = 1U;
    f.write.candidate.ready = 1U;
    const uint32_t burst_sectors[] = {16U, 32U, 64U};
    for (uint32_t i = 0U; i < 3U; ++i)
    {
        f.write.candidate.sector_count = burst_sectors[i];
        assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
        assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
        assert(f.write.last_grant == burst_sectors[i]);
    }
    f.read.candidate.margin_us = 5000U;
    f.write.candidate.margin_us = 1000U;
    f.write.candidate.sector_count = 64U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    assert(f.write.last_grant == 30U);
    assert(f.scheduler.metrics.write_burst_limits != 0U);
    assert(f.scheduler.metrics.read_to_write_switches != 0U);
    assert(f.scheduler.metrics.write_to_read_switches != 0U);
}

static void test_k_non_preemptive_slow_transaction(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.candidate.ready = 1U;
    f.read.polls_to_complete = 25U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    /* start_one completed it; repeat and inspect ownership while active. */
    f.read.candidate.ready = 1U;
    service(&f);
    assert(sd_scheduler_owner(&f.scheduler) == SD_SCHEDULER_OWNER_READ_DMA);
    f.write.candidate.ready = 1U;
    f.write.candidate.margin_us = 100000U;
    for (uint32_t i = 0U; i < 25U; ++i)
    {
        service(&f);
        assert(f.write.starts == 0U);
    }
    finish_active(&f);
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    assert(f.scheduler.metrics.starvation_prevented != 0U);
}

static void test_l_m_errors(void)
{
    fixture_t f;
    fixture_init(&f);
    f.write.candidate.ready = 1U;
    f.write.start_result = SD_SCHEDULER_START_BUSY;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    assert(f.scheduler.metrics.busy_rejects == 1U);
    assert(f.scheduler.metrics.write_transactions == 0U);
    f.write.start_result = SD_SCHEDULER_START_STARTED;
    f.write.candidate.ready = 1U;
    f.write.completion_result = SD_SCHEDULER_POLL_ERROR;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
    f.read.candidate.ready = 1U;
    f.read.completion_result = SD_SCHEDULER_POLL_ERROR;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_READ);
    assert(f.scheduler.metrics.errors == 2U);
    f.write.candidate.ready = 1U;
    f.write.completion_result = SD_SCHEDULER_POLL_COMPLETED;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_WRITE);
}

static void test_n_media_epoch_and_recovery(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.candidate.ready = 1U;
    f.read.candidate.media_epoch = f.epoch - 1U;
    assert(start_one(&f) == SD_SCHEDULER_CLASS_NONE);
    assert(f.scheduler.metrics.errors == 1U);
    f.read.candidate.media_epoch = f.epoch;
    f.read.completion_result = SD_SCHEDULER_POLL_RECOVERY_ABORT;
    f.read.polls_to_complete = 1U;
    service(&f);
    assert(sd_scheduler_owner(&f.scheduler) == SD_SCHEDULER_OWNER_READ_DMA);
    service(&f);
    assert(sd_scheduler_owner(&f.scheduler) == SD_SCHEDULER_OWNER_RECOVERY_ABORT);
    f.read.completion_result = SD_SCHEDULER_POLL_COMPLETED;
    service(&f);
    assert(sd_scheduler_owner(&f.scheduler) == SD_SCHEDULER_OWNER_IDLE);
}

static void test_streaming_recording_pressure(void)
{
    fixture_t f;
    fixture_init(&f);
    f.read.persistent = 1U;
    f.write.persistent = 1U;
    f.read.candidate.ready = 1U;
    f.write.candidate.ready = 1U;
    f.read.candidate.sector_count = 64U;
    f.write.candidate.sector_count = 36U; /* PCM24 stereo @ 48 kHz quantum. */
    f.read.candidate.margin_us = 60000U;
    f.write.candidate.margin_us = 60000U;
    uint32_t read_wins = 0U;
    uint32_t write_wins = 0U;
    for (uint32_t pressure = 0U; pressure < 60U; ++pressure)
    {
        f.read.candidate.margin_us = 60000U - (pressure * 900U);
        f.write.candidate.margin_us = 30000U - (pressure * 300U);
        const sd_scheduler_class_t picked = start_one(&f);
        read_wins += (picked == SD_SCHEDULER_CLASS_READ) ? 1U : 0U;
        write_wins += (picked == SD_SCHEDULER_CLASS_WRITE) ? 1U : 0U;
    }
    assert(read_wins != 0U);
    assert(write_wins != 0U);
    assert(f.scheduler.metrics.urgent_read_decisions != 0U);
    assert(f.scheduler.metrics.urgent_write_decisions != 0U);
    assert(f.scheduler.metrics.min_read_margin_us <= 10000U);
    assert(f.scheduler.metrics.min_write_margin_us <= 20000U);
}

static uint32_t run_synthetic_recorder(uint32_t rate_percent,
                                       uint8_t inject_burst)
{
    sd_scheduler_t scheduler;
    sd_scheduler_config_t config;
    sd_scheduler_default_config(&config);
    config.worst_case_us_per_sector = 100U;
    sd_scheduler_init(&scheduler, &config);
    synthetic_write_ring_t ring;
    memset(&ring, 0, sizeof(ring));
    ring.capacity_bytes = 576000U; /* two seconds of PCM24 stereo at 48 kHz. */
    ring.bytes_per_second = 288000U;
    ring.epoch = 11U;
    const sd_scheduler_provider_t provider = {
        .context = &ring,
        .peek = synthetic_write_peek,
        .start = synthetic_write_start,
        .poll = synthetic_write_poll,
    };
    assert(sd_scheduler_bind_provider(
               &scheduler, SD_SCHEDULER_CLASS_WRITE, &provider)
           != 0U);
    for (uint32_t step = 0U; step < 200U; ++step)
    {
        uint32_t applied_rate = rate_percent;
        if ((inject_burst != 0U) && (step >= 80U) && (step < 100U))
        {
            applied_rate = 250U;
        }
        synthetic_write_produce(&ring, 10000U, applied_rate);
        sd_scheduler_service(&scheduler, step * 10000U, ring.epoch);
        sd_scheduler_service(&scheduler, step * 10000U + 1000U, ring.epoch);
        assert(ring.fill_bytes < ring.capacity_bytes);
    }
    assert(ring.starts != 0U);
    assert(ring.written_bytes != 0U);
    return ring.starts;
}

static void test_synthetic_recorder_rates(void)
{
    const uint32_t slow = run_synthetic_recorder(50U, 0U);
    const uint32_t nominal = run_synthetic_recorder(100U, 0U);
    const uint32_t high = run_synthetic_recorder(160U, 0U);
    const uint32_t burst = run_synthetic_recorder(100U, 1U);
    assert(slow < nominal);
    assert(nominal < high);
    assert(nominal < burst);
}

int main(void)
{
    test_a_b_single_class();
    test_c_to_f_deadlines();
    test_g_h_no_starvation();
    test_i_filesystem_watermarks();
    test_j_switches_and_bursts();
    test_k_non_preemptive_slow_transaction();
    test_l_m_errors();
    test_n_media_epoch_and_recovery();
    test_streaming_recording_pressure();
    test_synthetic_recorder_rates();
    puts("sd_scheduler_test: PASS (A-N, READ 4/8/16/32 KiB, WRITE 8/16/32 KiB, FS 5s/2s/500ms)");
    return 0;
}
