#include <assert.h>
#include <string.h>

#include "SD/sd_scheduler.h"

typedef struct
{
    sd_scheduler_class_t type;
    uint32_t media_epoch;
    uint8_t ready;
} test_provider_t;

static uint8_t test_peek(void *context, sd_scheduler_candidate_t *candidate)
{
    test_provider_t *const provider = context;
    if ((provider == 0) || (candidate == 0) || (provider->ready == 0U))
    {
        return 0U;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = provider->type;
    candidate->ready = 1U;
    candidate->media_epoch = provider->media_epoch;
    candidate->sector_count = 1U;
    return 1U;
}

static sd_scheduler_start_result_t test_start(
    void *context,
    const sd_scheduler_candidate_t *candidate,
    uint32_t granted_sector_count)
{
    (void)context;
    (void)candidate;
    (void)granted_sector_count;
    return SD_SCHEDULER_START_COMPLETED;
}

static sd_scheduler_poll_result_t test_poll(void *context)
{
    (void)context;
    return SD_SCHEDULER_POLL_COMPLETED;
}

static void bind_test_provider(sd_scheduler_t *scheduler,
                               test_provider_t *state)
{
    const sd_scheduler_provider_t provider = {
        .context = state,
        .peek = test_peek,
        .start = test_start,
        .poll = (state->type == SD_SCHEDULER_CLASS_FILESYSTEM)
                    ? 0 : test_poll,
    };
    assert(sd_scheduler_bind_provider(scheduler, state->type, &provider) != 0U);
}

int main(void)
{
    sd_scheduler_t scheduler;
    sd_scheduler_init(&scheduler, 0);
    assert(sd_scheduler_background_can_start(&scheduler, 7U) != 0U);

    test_provider_t read = {
        .type = SD_SCHEDULER_CLASS_READ,
        .media_epoch = 7U,
    };
    test_provider_t write = {
        .type = SD_SCHEDULER_CLASS_WRITE,
        .media_epoch = 7U,
    };
    test_provider_t filesystem = {
        .type = SD_SCHEDULER_CLASS_FILESYSTEM,
        .media_epoch = 7U,
    };
    bind_test_provider(&scheduler, &read);
    bind_test_provider(&scheduler, &write);
    bind_test_provider(&scheduler, &filesystem);

    read.ready = 1U;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) == 0U);
    read.ready = 0U;
    write.ready = 1U;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) == 0U);
    write.ready = 0U;
    filesystem.ready = 1U;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) == 0U);
    filesystem.ready = 0U;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) != 0U);

    scheduler.owner = SD_SCHEDULER_OWNER_READ_DMA;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) == 0U);
    scheduler.owner = SD_SCHEDULER_OWNER_IDLE;
    assert(sd_scheduler_background_can_start(&scheduler, 7U) != 0U);
    return 0;
}
