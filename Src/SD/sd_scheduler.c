#include "SD/sd_scheduler.h"

#include <assert.h>
#include <string.h>

typedef struct
{
    sd_scheduler_candidate_t candidate[SD_SCHEDULER_CLASS_COUNT];
    uint8_t available[SD_SCHEDULER_CLASS_COUNT];
    uint8_t urgent[SD_SCHEDULER_CLASS_COUNT];
} sd_scheduler_snapshot_t;

static uint32_t sd_scheduler_elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

static uint32_t sd_scheduler_max_u32(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static uint32_t sd_scheduler_cost(const sd_scheduler_t *scheduler,
                                  const sd_scheduler_candidate_t *candidate,
                                  uint32_t sectors)
{
    uint32_t estimate = candidate->estimated_cost_us;
    if ((candidate->sector_count != 0U) && (sectors < candidate->sector_count))
    {
        const uint64_t scaled =
            ((uint64_t)candidate->estimated_cost_us * sectors
             + candidate->sector_count - 1U)
            / candidate->sector_count;
        estimate = (scaled > UINT32_MAX) ? UINT32_MAX : (uint32_t)scaled;
    }
    if (sectors != 0U)
    {
        const uint64_t configured =
            (uint64_t)sectors * scheduler->config.worst_case_us_per_sector;
        estimate = sd_scheduler_max_u32(
            estimate,
            (configured > UINT32_MAX) ? UINT32_MAX : (uint32_t)configured);
    }
    return estimate;
}

void sd_scheduler_default_config(sd_scheduler_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    config->critical_margin_us = 20000U;
    config->reservation_low_margin_us = 2000000U;
    config->reservation_critical_margin_us = 500000U;
    config->transaction_guard_us = 2000U;
    config->worst_case_us_per_sector = 250U;
    config->max_write_sectors = 64U;
    config->starvation_limit_us = 100000U;
}

void sd_scheduler_init(sd_scheduler_t *scheduler,
                       const sd_scheduler_config_t *config)
{
    if (scheduler == 0)
    {
        return;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    if (config != 0)
    {
        scheduler->config = *config;
    }
    else
    {
        sd_scheduler_default_config(&scheduler->config);
    }
    scheduler->owner = SD_SCHEDULER_OWNER_IDLE;
    scheduler->round_robin_cursor = (uint8_t)SD_SCHEDULER_CLASS_READ;
    scheduler->metrics.min_read_margin_us = SD_SCHEDULER_MARGIN_UNKNOWN;
    scheduler->metrics.min_write_margin_us = SD_SCHEDULER_MARGIN_UNKNOWN;
    scheduler->metrics.min_reservation_margin_us = SD_SCHEDULER_MARGIN_UNKNOWN;
}

uint8_t sd_scheduler_bind_provider(sd_scheduler_t *scheduler,
                                   sd_scheduler_class_t type,
                                   const sd_scheduler_provider_t *provider)
{
    if ((scheduler == 0) || (provider == 0)
        || (type <= SD_SCHEDULER_CLASS_NONE)
        || (type >= SD_SCHEDULER_CLASS_COUNT) || (provider->peek == 0)
        || (provider->start == 0)
        || ((type != SD_SCHEDULER_CLASS_FILESYSTEM) && (provider->poll == 0)))
    {
        return 0U;
    }
    scheduler->providers[type] = *provider;
    return 1U;
}

static void sd_scheduler_note_wait(sd_scheduler_t *scheduler,
                                   sd_scheduler_class_t type,
                                   uint32_t now,
                                   uint8_t ready)
{
    if (ready == 0U)
    {
        scheduler->wait_active[type] = 0U;
        return;
    }
    if (scheduler->wait_active[type] == 0U)
    {
        scheduler->wait_active[type] = 1U;
        scheduler->wait_since_us[type] = now;
    }
}

static uint32_t sd_scheduler_wait(const sd_scheduler_t *scheduler,
                                  sd_scheduler_class_t type,
                                  uint32_t now)
{
    return (scheduler->wait_active[type] != 0U)
               ? sd_scheduler_elapsed(now, scheduler->wait_since_us[type])
               : 0U;
}

static void sd_scheduler_record_margin(sd_scheduler_t *scheduler,
                                       const sd_scheduler_candidate_t *candidate)
{
    uint32_t *minimum = 0;
    if (candidate->type == SD_SCHEDULER_CLASS_READ)
    {
        minimum = &scheduler->metrics.min_read_margin_us;
    }
    else if (candidate->type == SD_SCHEDULER_CLASS_WRITE)
    {
        minimum = &scheduler->metrics.min_write_margin_us;
    }
    else if (candidate->type == SD_SCHEDULER_CLASS_FILESYSTEM)
    {
        minimum = &scheduler->metrics.min_reservation_margin_us;
    }
    if ((minimum != 0) && (candidate->margin_us < *minimum))
    {
        *minimum = candidate->margin_us;
    }
}

static void sd_scheduler_collect(sd_scheduler_t *scheduler,
                                 uint32_t now,
                                 uint32_t media_epoch,
                                 sd_scheduler_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    for (uint8_t raw_type = (uint8_t)SD_SCHEDULER_CLASS_READ;
         raw_type < (uint8_t)SD_SCHEDULER_CLASS_COUNT;
         ++raw_type)
    {
        const sd_scheduler_class_t type = (sd_scheduler_class_t)raw_type;
        sd_scheduler_provider_t *const provider = &scheduler->providers[type];
        sd_scheduler_candidate_t *const candidate = &snapshot->candidate[type];
        if ((provider->peek == 0)
            || (provider->peek(provider->context, candidate) == 0U)
            || (candidate->ready == 0U))
        {
            sd_scheduler_note_wait(scheduler, type, now, 0U);
            continue;
        }
        const uint8_t invalid_transport =
            ((type != SD_SCHEDULER_CLASS_FILESYSTEM)
             && ((candidate->sector_count == 0U)
                 || ((type == SD_SCHEDULER_CLASS_READ)
                     && (candidate->read_buffer == 0))
                 || ((type == SD_SCHEDULER_CLASS_WRITE)
                     && (candidate->write_buffer == 0))))
                ? 1U
                : 0U;
        if ((candidate->type != type) || (candidate->media_epoch != media_epoch)
            || (invalid_transport != 0U))
        {
            scheduler->metrics.errors++;
            sd_scheduler_note_wait(scheduler, type, now, 0U);
            continue;
        }
        snapshot->available[type] = 1U;
        sd_scheduler_note_wait(scheduler, type, now, 1U);
        sd_scheduler_record_margin(scheduler, candidate);
        if (type == SD_SCHEDULER_CLASS_FILESYSTEM)
        {
            if ((candidate->reservation == SD_SCHEDULER_RESERVATION_CRITICAL)
                || (candidate->margin_us
                    <= scheduler->config.reservation_critical_margin_us))
            {
                scheduler->metrics.reservation_policy_failures++;
            }
            snapshot->urgent[type] =
                ((candidate->reservation != SD_SCHEDULER_RESERVATION_SAFE)
                 || (candidate->margin_us
                     <= scheduler->config.reservation_low_margin_us))
                    ? 1U
                    : 0U;
        }
        else
        {
            snapshot->urgent[type] =
                (candidate->margin_us <= scheduler->config.critical_margin_us)
                    ? 1U
                    : 0U;
        }
    }
}

static sd_scheduler_class_t sd_scheduler_pick_rr(sd_scheduler_t *scheduler,
                                                  const sd_scheduler_snapshot_t *s)
{
    for (uint8_t offset = 0U; offset < 3U; ++offset)
    {
        const uint8_t raw =
            (uint8_t)(((scheduler->round_robin_cursor - 1U + offset) % 3U) + 1U);
        const sd_scheduler_class_t type = (sd_scheduler_class_t)raw;
        if (s->available[type] != 0U)
        {
            return type;
        }
    }
    return SD_SCHEDULER_CLASS_NONE;
}

static sd_scheduler_class_t sd_scheduler_pick(sd_scheduler_t *scheduler,
                                               const sd_scheduler_snapshot_t *s,
                                               uint32_t now)
{
    const uint8_t read_urgent = s->urgent[SD_SCHEDULER_CLASS_READ];
    const uint8_t write_urgent = s->urgent[SD_SCHEDULER_CLASS_WRITE];
    if ((read_urgent != 0U) || (write_urgent != 0U))
    {
        sd_scheduler_class_t picked;
        if ((read_urgent != 0U) && (write_urgent != 0U))
        {
            const uint32_t read_margin =
                s->candidate[SD_SCHEDULER_CLASS_READ].margin_us;
            const uint32_t write_margin =
                s->candidate[SD_SCHEDULER_CLASS_WRITE].margin_us;
            if (read_margin < write_margin)
            {
                picked = SD_SCHEDULER_CLASS_READ;
            }
            else
            {
                picked = SD_SCHEDULER_CLASS_WRITE;
                if (read_margin == write_margin)
                {
                    scheduler->metrics.critical_ties++;
                }
            }
        }
        else
        {
            picked = (read_urgent != 0U) ? SD_SCHEDULER_CLASS_READ
                                         : SD_SCHEDULER_CLASS_WRITE;
        }
        if (picked == SD_SCHEDULER_CLASS_READ)
        {
            scheduler->metrics.urgent_read_decisions++;
        }
        else
        {
            scheduler->metrics.urgent_write_decisions++;
        }
        return picked;
    }

    if ((s->available[SD_SCHEDULER_CLASS_FILESYSTEM] != 0U)
        && (s->urgent[SD_SCHEDULER_CLASS_FILESYSTEM] != 0U))
    {
        return SD_SCHEDULER_CLASS_FILESYSTEM;
    }

    sd_scheduler_class_t starved = SD_SCHEDULER_CLASS_NONE;
    uint32_t greatest_wait = 0U;
    for (uint8_t raw_type = (uint8_t)SD_SCHEDULER_CLASS_READ;
         raw_type < (uint8_t)SD_SCHEDULER_CLASS_COUNT;
         ++raw_type)
    {
        const sd_scheduler_class_t type = (sd_scheduler_class_t)raw_type;
        if (s->available[type] == 0U)
        {
            continue;
        }
        const uint32_t wait = sd_scheduler_wait(scheduler, type, now);
        if ((wait >= scheduler->config.starvation_limit_us)
            && ((starved == SD_SCHEDULER_CLASS_NONE) || (wait > greatest_wait)))
        {
            starved = type;
            greatest_wait = wait;
        }
    }
    if (starved != SD_SCHEDULER_CLASS_NONE)
    {
        scheduler->metrics.starvation_prevented++;
        return starved;
    }
    return sd_scheduler_pick_rr(scheduler, s);
}

static sd_scheduler_class_t sd_scheduler_protect_next_deadline(
    const sd_scheduler_t *scheduler,
    const sd_scheduler_snapshot_t *snapshot,
    sd_scheduler_class_t picked)
{
    if (picked == SD_SCHEDULER_CLASS_NONE)
    {
        return picked;
    }
    if (((picked == SD_SCHEDULER_CLASS_READ)
         || (picked == SD_SCHEDULER_CLASS_WRITE))
        && (snapshot->urgent[picked] != 0U))
    {
        return picked;
    }
    const sd_scheduler_candidate_t *const chosen = &snapshot->candidate[picked];
    const uint32_t chosen_cost = sd_scheduler_cost(
        scheduler, chosen, chosen->sector_count);
    const uint64_t blocked_until =
        (uint64_t)chosen_cost + scheduler->config.transaction_guard_us;
    sd_scheduler_class_t threatened = SD_SCHEDULER_CLASS_NONE;
    uint32_t smallest_margin = UINT32_MAX;
    for (sd_scheduler_class_t type = SD_SCHEDULER_CLASS_READ;
         type <= SD_SCHEDULER_CLASS_WRITE;
         type = (sd_scheduler_class_t)(type + 1))
    {
        if ((type == picked) || (snapshot->available[type] == 0U))
        {
            continue;
        }
        const uint32_t margin = snapshot->candidate[type].margin_us;
        if (((uint64_t)margin <= blocked_until) && (margin < smallest_margin))
        {
            threatened = type;
            smallest_margin = margin;
        }
    }
    return (threatened != SD_SCHEDULER_CLASS_NONE) ? threatened : picked;
}

static uint32_t sd_scheduler_grant_sectors(sd_scheduler_t *scheduler,
                                           const sd_scheduler_snapshot_t *s,
                                           sd_scheduler_class_t type)
{
    const sd_scheduler_candidate_t *const candidate = &s->candidate[type];
    uint32_t granted = candidate->sector_count;
    if ((type == SD_SCHEDULER_CLASS_WRITE)
        && (granted > scheduler->config.max_write_sectors))
    {
        granted = scheduler->config.max_write_sectors;
    }
    if ((type == SD_SCHEDULER_CLASS_WRITE)
        && (s->available[SD_SCHEDULER_CLASS_READ] != 0U)
        && (scheduler->config.worst_case_us_per_sector != 0U))
    {
        uint32_t cost_per_sector = scheduler->config.worst_case_us_per_sector;
        if (candidate->sector_count != 0U)
        {
            const uint64_t rounded_cost =
                (uint64_t)candidate->estimated_cost_us
                + candidate->sector_count - 1U;
            const uint32_t candidate_cost_per_sector =
                (uint32_t)(rounded_cost / candidate->sector_count);
            cost_per_sector = sd_scheduler_max_u32(
                cost_per_sector, candidate_cost_per_sector);
        }
        const uint32_t read_margin =
            s->candidate[SD_SCHEDULER_CLASS_READ].margin_us;
        const uint32_t safe_time =
            (read_margin > scheduler->config.transaction_guard_us)
                ? read_margin - scheduler->config.transaction_guard_us
                : 0U;
        uint32_t safe_sectors = safe_time / cost_per_sector;
        if (safe_sectors == 0U)
        {
            safe_sectors = 1U;
        }
        if (granted > safe_sectors)
        {
            granted = safe_sectors;
        }
    }
    if (granted < candidate->sector_count)
    {
        scheduler->metrics.write_burst_limits++;
    }
    return granted;
}

static void sd_scheduler_update_max_wait(sd_scheduler_t *scheduler,
                                         sd_scheduler_class_t type,
                                         uint32_t now)
{
    if ((type <= SD_SCHEDULER_CLASS_NONE)
        || (type >= SD_SCHEDULER_CLASS_COUNT))
    {
        return;
    }
    const uint32_t wait = sd_scheduler_wait(scheduler, type, now);
    uint32_t *maximum = 0;
    if (type == SD_SCHEDULER_CLASS_READ)
    {
        maximum = &scheduler->metrics.max_read_wait_us;
    }
    else if (type == SD_SCHEDULER_CLASS_WRITE)
    {
        maximum = &scheduler->metrics.max_write_wait_us;
    }
    else if (type == SD_SCHEDULER_CLASS_FILESYSTEM)
    {
        maximum = &scheduler->metrics.max_filesystem_wait_us;
    }
    if ((maximum != 0) && (wait > *maximum))
    {
        *maximum = wait;
    }
    scheduler->wait_active[type] = 0U;
}

static void sd_scheduler_claim(sd_scheduler_t *scheduler,
                               sd_scheduler_class_t type)
{
    if (type == SD_SCHEDULER_CLASS_READ)
    {
        scheduler->owner = SD_SCHEDULER_OWNER_READ_DMA;
    }
    else if (type == SD_SCHEDULER_CLASS_WRITE)
    {
        scheduler->owner = SD_SCHEDULER_OWNER_WRITE_DMA;
    }
    else
    {
        scheduler->owner = SD_SCHEDULER_OWNER_FILESYSTEM;
    }
    scheduler->active_class = type;
}

static void sd_scheduler_note_accepted(sd_scheduler_t *scheduler,
                                       sd_scheduler_class_t type,
                                       uint32_t now)
{
    sd_scheduler_update_max_wait(scheduler, type, now);
    if (type == SD_SCHEDULER_CLASS_READ)
    {
        scheduler->metrics.read_transactions++;
    }
    else if (type == SD_SCHEDULER_CLASS_WRITE)
    {
        scheduler->metrics.write_transactions++;
    }
    else
    {
        scheduler->metrics.filesystem_slots++;
    }
    if ((scheduler->last_dma_class == SD_SCHEDULER_CLASS_READ)
        && (type == SD_SCHEDULER_CLASS_WRITE))
    {
        scheduler->metrics.read_to_write_switches++;
    }
    else if ((scheduler->last_dma_class == SD_SCHEDULER_CLASS_WRITE)
             && (type == SD_SCHEDULER_CLASS_READ))
    {
        scheduler->metrics.write_to_read_switches++;
    }
    if (type != SD_SCHEDULER_CLASS_FILESYSTEM)
    {
        scheduler->last_dma_class = type;
    }
    scheduler->round_robin_cursor = (uint8_t)((type % 3U) + 1U);
}

static void sd_scheduler_poll_active(sd_scheduler_t *scheduler)
{
    assert(scheduler->active_class > SD_SCHEDULER_CLASS_NONE);
    assert(scheduler->active_class < SD_SCHEDULER_CLASS_COUNT);
    sd_scheduler_provider_t *const provider =
        &scheduler->providers[scheduler->active_class];
    assert(provider->poll != 0);
    const sd_scheduler_poll_result_t result = provider->poll(provider->context);
    if (result == SD_SCHEDULER_POLL_ACTIVE)
    {
        return;
    }
    if (result == SD_SCHEDULER_POLL_RECOVERY_ABORT)
    {
        scheduler->owner = SD_SCHEDULER_OWNER_RECOVERY_ABORT;
        return;
    }
    if (result == SD_SCHEDULER_POLL_ERROR)
    {
        scheduler->metrics.errors++;
    }
    scheduler->owner = SD_SCHEDULER_OWNER_IDLE;
    scheduler->active_class = SD_SCHEDULER_CLASS_NONE;
}

void sd_scheduler_service(sd_scheduler_t *scheduler,
                          uint32_t now_us,
                          uint32_t media_epoch)
{
    if (scheduler == 0)
    {
        return;
    }
    if (scheduler->owner != SD_SCHEDULER_OWNER_IDLE)
    {
        if (scheduler->owner == SD_SCHEDULER_OWNER_FILESYSTEM)
        {
            assert(0 && "filesystem work must complete synchronously");
            return;
        }
        sd_scheduler_snapshot_t waiting_snapshot;
        sd_scheduler_collect(scheduler, now_us, media_epoch, &waiting_snapshot);
        scheduler->wait_active[scheduler->active_class] = 0U;
        sd_scheduler_poll_active(scheduler);
        return;
    }

    scheduler->active_media_epoch = media_epoch;
    sd_scheduler_snapshot_t snapshot;
    sd_scheduler_collect(scheduler, now_us, media_epoch, &snapshot);
    sd_scheduler_class_t picked = sd_scheduler_pick(scheduler, &snapshot, now_us);
    picked = sd_scheduler_protect_next_deadline(scheduler, &snapshot, picked);
    if (picked == SD_SCHEDULER_CLASS_NONE)
    {
        return;
    }
    sd_scheduler_provider_t *const provider = &scheduler->providers[picked];
    const uint32_t sectors =
        sd_scheduler_grant_sectors(scheduler, &snapshot, picked);
    sd_scheduler_claim(scheduler, picked);
    const sd_scheduler_start_result_t result = provider->start(
        provider->context, &snapshot.candidate[picked], sectors);
    if (result == SD_SCHEDULER_START_STARTED)
    {
        assert(picked != SD_SCHEDULER_CLASS_FILESYSTEM);
        sd_scheduler_note_accepted(scheduler, picked, now_us);
        return;
    }
    if (result == SD_SCHEDULER_START_BUSY)
    {
        scheduler->metrics.busy_rejects++;
    }
    else if (result == SD_SCHEDULER_START_ERROR)
    {
        scheduler->metrics.errors++;
    }
    else
    {
        assert(picked == SD_SCHEDULER_CLASS_FILESYSTEM);
        sd_scheduler_note_accepted(scheduler, picked, now_us);
    }
    scheduler->owner = SD_SCHEDULER_OWNER_IDLE;
    scheduler->active_class = SD_SCHEDULER_CLASS_NONE;
}

uint8_t sd_scheduler_background_can_start(sd_scheduler_t *scheduler,
                                          uint32_t media_epoch)
{
    (void)media_epoch;
    if ((scheduler == 0) || (scheduler->owner != SD_SCHEDULER_OWNER_IDLE))
    {
        return 0U;
    }

    /*
     * Background admission is deliberately stricter than deadline
     * arbitration: any ready RT data or recorder-continuity request wins.
     * Peeking is side-effect free by provider contract and does not alter the
     * normal scheduler round-robin or wait accounting.
     */
    for (uint8_t raw_type = (uint8_t)SD_SCHEDULER_CLASS_READ;
         raw_type < (uint8_t)SD_SCHEDULER_CLASS_COUNT;
         ++raw_type)
    {
        const sd_scheduler_class_t type = (sd_scheduler_class_t)raw_type;
        sd_scheduler_provider_t *const provider = &scheduler->providers[type];
        sd_scheduler_candidate_t candidate;
        memset(&candidate, 0, sizeof(candidate));
        if ((provider->peek != 0)
            && (provider->peek(provider->context, &candidate) != 0U)
            && (candidate.ready != 0U))
        {
            return 0U;
        }
    }
    return 1U;
}

sd_scheduler_owner_t sd_scheduler_owner(const sd_scheduler_t *scheduler)
{
    return (scheduler != 0) ? scheduler->owner : SD_SCHEDULER_OWNER_IDLE;
}

void sd_scheduler_metrics_get(const sd_scheduler_t *scheduler,
                              sd_scheduler_metrics_t *metrics)
{
    if ((scheduler != 0) && (metrics != 0))
    {
        *metrics = scheduler->metrics;
    }
}
