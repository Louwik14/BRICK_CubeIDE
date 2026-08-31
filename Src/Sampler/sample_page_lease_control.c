#include "Sampler/sample_page_lease_control.h"

#include <string.h>

#include "stm32h7xx.h"

uint8_t sample_page_lease_control_read(uint8_t slot,
                                       sample_page_lease_t *out)
{
    if ((slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) || (out == NULL)) return 0U;
    const sample_page_lease_t *const lease = &g_sample_page_leases[slot];
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = lease->seq;
        __DMB();
        if ((before == 0U) || ((before & 1U) != 0U)) continue;
        sample_page_lease_t copy = *lease;
        __DMB();
        if (before == lease->seq)
        {
            *out = copy;
            return (copy.ranges[0].page_count != 0U) ? 1U : 0U;
        }
    }
    memset(out, 0, sizeof(*out));
    return 0U;
}

uint8_t sample_page_lease_control_protects(sample_audio_key_t key,
                                           uint32_t registration_epoch,
                                           uint32_t page_index)
{
    sample_page_lease_t lease;
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        const uint32_t observed_seq = g_sample_page_leases[slot].seq;
        if (sample_page_lease_control_read(slot, &lease) == 0U)
        {
            /* A concurrently changing non-empty slot is conservatively
             * protective; eviction retries on a later CONTROL pass. */
            if (observed_seq != 0U
                && ((observed_seq & 1U) != 0U
                    || observed_seq != g_sample_page_leases[slot].seq)) return 1U;
            continue;
        }
        if (((lease.registration_epoch != 0U)
                && (lease.registration_epoch != registration_epoch))
            || (sample_audio_key_equal(&lease.key, &key) == 0U)) continue;
        for (uint8_t range = 0U; range < 2U; ++range)
        {
            const uint32_t first = lease.ranges[range].first_page;
            const uint32_t count = lease.ranges[range].page_count;
            if ((count != 0U) && (page_index >= first)
                && (page_index < (first + count))) return 1U;
        }
    }
    return 0U;
}

uint8_t sample_page_lease_control_references_key(sample_audio_key_t key)
{
    sample_page_lease_t lease;
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        const uint32_t observed_seq = g_sample_page_leases[slot].seq;
        if (sample_page_lease_control_read(slot, &lease) == 0U)
        {
            if ((observed_seq != 0U)
                && (((observed_seq & 1U) != 0U)
                    || (observed_seq != g_sample_page_leases[slot].seq)))
                return 1U;
            continue;
        }
        if (sample_audio_key_equal(&lease.key, &key) != 0U) return 1U;
    }
    return 0U;
}

uint8_t sample_page_lease_control_all_released(void)
{
    sample_page_lease_t lease;
    for (uint8_t slot = 0U; slot < SAMPLE_PAGE_LEASE_SLOT_COUNT; ++slot)
    {
        const uint32_t observed_seq = g_sample_page_leases[slot].seq;
        if (sample_page_lease_control_read(slot, &lease) != 0U) return 0U;
        if ((observed_seq != 0U)
            && (((observed_seq & 1U) != 0U)
                || (observed_seq != g_sample_page_leases[slot].seq))) return 0U;
    }
    return 1U;
}
