#include "Sampler/sample_page_lease_control.h"

#include <string.h>

#include "stm32h7xx.h"
#include "Platform/stream_rec_perf.h"

void sample_page_lease_control_active_slots(uint32_t out_mask[2])
{
    if (out_mask == NULL) return;
    __DMB();
    out_mask[0] = g_sample_page_lease_active_mask[0];
    out_mask[1] = g_sample_page_lease_active_mask[1];
    __DMB();
}

uint8_t sample_page_lease_control_snapshot_active(
    sample_page_lease_active_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return 0U;
    out_snapshot->resolved_count = 0U;
    out_snapshot->unresolved_count = 0U;
    out_snapshot->reader_count = 0U;
    memset(out_snapshot->protected_slots, 0,
           sizeof(out_snapshot->protected_slots));
    uint32_t before[2];
    sample_page_lease_control_active_slots(before);
    for (uint8_t word_index = 0U; word_index < 2U; ++word_index)
    {
        uint32_t active = before[word_index];
        while (active != 0U)
        {
            const uint8_t bit = (uint8_t)__builtin_ctz(active);
            const uint8_t slot = (uint8_t)(word_index * 32U + bit);
            active &= active - 1U;
            sample_page_lease_t lease;
            if ((slot >= SAMPLE_PAGE_LEASE_SLOT_COUNT)
                || (out_snapshot->reader_count >= SAMPLE_STREAM_ACTIVE_READER_CAPACITY)
                || (sample_page_lease_control_read(slot, &lease) == 0U))
                return 0U;
            out_snapshot->reader_slot[out_snapshot->reader_count] = slot;
            out_snapshot->reader_seq[out_snapshot->reader_count] = lease.seq;
            ++out_snapshot->reader_count;
            for (uint8_t role = 0U; role < SAMPLE_PAGE_LEASE_PAGE_COUNT; ++role)
            {
                if ((lease.valid_mask & SAMPLE_PAGE_LEASE_VALID(role)) == 0U)
                    continue;
                const sample_reader_window_role_t *const window = &lease.roles[role];
                if ((window->resolved != 0U)
                    && (window->slot_index < SAMPLE_PAGE_MAX_COUNT))
                {
                    out_snapshot->protected_slots[
                        window->slot_index >> 5U]
                        |= UINT32_C(1)
                           << (window->slot_index & 31U);
                    sample_page_resolved_protection_t *const protection =
                        &out_snapshot->resolved[out_snapshot->resolved_count++];
                    protection->slot_index = window->slot_index;
                    protection->page_generation = window->page_generation;
                }
                else
                {
                    sample_page_logical_protection_t *const protection =
                        &out_snapshot->unresolved[out_snapshot->unresolved_count++];
                    protection->key = window->key;
                    protection->registration_epoch = lease.registration_epoch;
                    protection->page_index = window->page_index;
                }
            }
        }
    }
    uint32_t after[2];
    sample_page_lease_control_active_slots(after);
    if ((before[0] != after[0]) || (before[1] != after[1])) return 0U;
    for (uint8_t i = 0U; i < out_snapshot->reader_count; ++i)
    {
        if (g_sample_page_leases[out_snapshot->reader_slot[i]].seq
            != out_snapshot->reader_seq[i]) return 0U;
    }
    return 1U;
}

uint8_t sample_page_lease_control_snapshot_protects(
    const sample_page_lease_active_snapshot_t *snapshot,
    uint16_t slot_index,
    uint32_t page_generation,
    sample_audio_key_t key,
    uint32_t registration_epoch,
    uint32_t page_index)
{
    if (snapshot == NULL) return 1U;
    const uint8_t physical_maybe_protected = (uint8_t)(
        (slot_index < SAMPLE_PAGE_MAX_COUNT)
        && ((snapshot->protected_slots[slot_index >> 5U]
             & (UINT32_C(1) << (slot_index & 31U))) != 0U));
    if (physical_maybe_protected != 0U)
    {
        for (uint8_t i = 0U; i < snapshot->resolved_count; ++i)
        {
            const sample_page_resolved_protection_t *const protection =
                &snapshot->resolved[i];
            if ((protection->slot_index == slot_index)
                && (protection->page_generation == page_generation)) return 1U;
        }
    }
    for (uint8_t i = 0U; i < snapshot->unresolved_count; ++i)
    {
        const sample_page_logical_protection_t *const protection =
            &snapshot->unresolved[i];
        if (((protection->registration_epoch == 0U)
                || (protection->registration_epoch == registration_epoch))
            && (protection->page_index == page_index)
            && (sample_audio_key_equal(&protection->key, &key) != 0U)) return 1U;
    }
    return 0U;
}

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
            return (copy.valid_mask != 0U) ? 1U : 0U;
        }
    }
    memset(out, 0, sizeof(*out));
    return 0U;
}

uint8_t sample_page_lease_control_protects(uint16_t slot_index,
                                           uint32_t page_generation,
                                           sample_audio_key_t key,
                                           uint32_t registration_epoch,
                                           uint32_t page_index)
{
    PERF_COUNT(PERF_N_LEASE_CHECK);
    sample_page_lease_active_snapshot_t snapshot;
    if (sample_page_lease_control_snapshot_active(&snapshot) == 0U)
        return 1U;
    return sample_page_lease_control_snapshot_protects(
        &snapshot, slot_index, page_generation, key,
        registration_epoch, page_index);
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
