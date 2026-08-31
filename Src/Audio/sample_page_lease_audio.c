#include "Audio/sample_page_lease_audio.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

D2_IPC sample_page_lease_t g_sample_page_leases[SAMPLE_PAGE_LEASE_SLOT_COUNT];

void sample_page_lease_audio_init(void)
{
    memset(g_sample_page_leases, 0, sizeof(g_sample_page_leases));
    __DMB();
}

uint8_t sample_page_lease_audio_publish(uint8_t slot,
                                       sample_audio_key_t key,
                                       uint32_t registration_epoch,
                                       const sample_page_lease_range_t ranges[2])
{
    if ((slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) || (ranges == NULL)) return 0U;
    sample_page_lease_t *const lease = &g_sample_page_leases[slot];
    if ((sample_audio_key_equal(&lease->key, &key) != 0U)
        && (lease->registration_epoch == registration_epoch)
        && (memcmp(lease->ranges, ranges, sizeof(lease->ranges)) == 0)) return 1U;
    uint32_t seq = lease->seq;
    if ((seq & 1U) != 0U) ++seq;
    lease->seq = seq + 1U;
    __DMB();
    lease->key = key;
    lease->registration_epoch = registration_epoch;
    lease->ranges[0] = ranges[0];
    lease->ranges[1] = ranges[1];
    __DMB();
    lease->seq = seq + 2U;
    __DMB();
    return 1U;
}

void sample_page_lease_audio_clear(uint8_t slot)
{
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) return;
    const sample_page_lease_range_t empty[2] = { {0}, {0} };
    const sample_audio_key_t key = { SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U };
    (void)sample_page_lease_audio_publish(slot, key, 0U, empty);
}
