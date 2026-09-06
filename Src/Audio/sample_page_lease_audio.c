#include "Audio/sample_page_lease_audio.h"

#include <string.h>

#include "IPC/storage_io_wakeup.h"
#include "IPC/control_rt_wakeup.h"
#include "Sampler/sample_stream_admission.h"
#include "stm32h7xx.h"

static void sample_stream_admission_audio_or_word(volatile uint32_t *word,
                                                  uint32_t bits)
{
    uint32_t old_value;
    uint32_t new_value;
    do
    {
        old_value = __LDREXW(word);
        new_value = old_value | bits;
    } while (__STREXW(new_value, word) != 0U);
    __DMB();
}

void sample_stream_admission_audio_signal_lease(uint8_t slot)
{
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) return;

    sample_stream_admission_release_stamp_t stamp = {0};
    for (uint8_t i = 0U; i < STREAM_MAX_ACTIVE_READERS; ++i)
    {
        const volatile sample_stream_admission_credit_t *const credit =
            &g_sample_stream_admission_ledger[i];
        if ((credit->physical_present != 0U)
            && (credit->physical_slot == slot))
        {
            stamp.generation = credit->generation;
            stamp.ledger_slot = i;
            stamp.owner = credit->owner;
            break;
        }
    }
    g_sample_stream_admission_release_stamps[slot] = stamp;
    __DMB();
    sample_stream_admission_audio_or_word(
        &g_sample_stream_admission_release_mask[slot / 32U],
        (uint32_t)1U << (slot % 32U));
}

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
    const uint8_t changed =
        (sample_audio_key_equal(&lease->key, &key) == 0U)
        || (lease->registration_epoch != registration_epoch)
        || (memcmp(lease->ranges, ranges, sizeof(lease->ranges)) != 0);
    if (changed == 0U)
    {
        return 1U;
    }
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
    if ((ranges[0].page_count == 0U) && (ranges[1].page_count == 0U))
    {
        sample_stream_admission_audio_signal_lease(slot);
        control_rt_wakeup(CONTROL_RT_WAKE_STREAM_RELEASE);
    }
    storage_io_owner_wakeup(STORAGE_OWNER_STREAM);
    if ((ranges[0].page_count == 0U) && (ranges[1].page_count == 0U))
        storage_io_owner_wakeup(STORAGE_OWNER_PROJECT);
    return 1U;
}

void sample_page_lease_audio_clear(uint8_t slot)
{
    if (slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT) return;
    const sample_page_lease_range_t empty[2] = { {0}, {0} };
    const sample_audio_key_t key = {
        .domain = SAMPLE_AUDIO_DOMAIN_CLASSIC, .object_id = 0U
    };
    (void)sample_page_lease_audio_publish(slot, key, 0U, empty);
}
