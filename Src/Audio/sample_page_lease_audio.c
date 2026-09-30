#include "Audio/sample_page_lease_audio.h"

#include <string.h>

#include "stm32h7xx.h"
#include "Platform/memory_layout.h"
#include "Platform/stream_rec_perf.h"

STREAM_LOCAL_D2 sample_page_lease_t
    g_sample_page_leases[SAMPLE_PAGE_LEASE_SLOT_COUNT];
STREAM_LOCAL_D2 volatile uint32_t g_sample_page_lease_active_mask[2];

void sample_page_lease_audio_init(void)
{
    memset(g_sample_page_leases, 0, sizeof(g_sample_page_leases));
    g_sample_page_lease_active_mask[0] = 0U;
    g_sample_page_lease_active_mask[1] = 0U;
    __DMB();
}

uint8_t sample_page_lease_audio_publish(uint8_t slot,
                                       sample_audio_key_t key,
                                       uint32_t registration_epoch,
                                       const uint32_t pages[SAMPLE_PAGE_LEASE_PAGE_COUNT],
                                       uint8_t valid_mask)
{
    if ((slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) || (pages == NULL)) return 0U;
    sample_page_lease_t *const lease = &g_sample_page_leases[slot];
    if ((sample_audio_key_equal(&lease->key, &key) != 0U)
        && (lease->registration_epoch == registration_epoch)
        && (lease->valid_mask == valid_mask)
        && (memcmp(lease->pages, pages, sizeof(lease->pages)) == 0)) return 1U;
    PERF_START(lease_start);
    uint32_t seq = lease->seq;
    if ((seq & 1U) != 0U) ++seq;
    lease->seq = seq + 1U;
    __DMB();
    lease->key = key;
    lease->registration_epoch = registration_epoch;
    memcpy(lease->pages, pages, sizeof(lease->pages));
    lease->valid_mask = valid_mask;
    __DMB();
    lease->seq = seq + 2U;
    __DMB();
    const uint8_t word = (uint8_t)(slot >> 5U);
    const uint32_t bit = UINT32_C(1) << (slot & 31U);
    if (valid_mask != 0U)
        g_sample_page_lease_active_mask[word] |= bit;
    else
        g_sample_page_lease_active_mask[word] &= ~bit;
    __DMB();
    PERF_END(PERF_CPU_READER_LEASE, lease_start);
    PERF_COUNT(PERF_N_LEASE_PUBLISH);
    return 1U;
}

void sample_page_lease_audio_clear(uint8_t slot)
{
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) return;
    const uint32_t empty[SAMPLE_PAGE_LEASE_PAGE_COUNT] = {0};
    const sample_audio_key_t key = { SAMPLE_AUDIO_DOMAIN_CLASSIC, 0U };
    (void)sample_page_lease_audio_publish(slot, key, 0U, empty, 0U);
}
