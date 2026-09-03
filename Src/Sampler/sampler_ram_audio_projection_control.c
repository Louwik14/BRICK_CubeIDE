#include "IPC/sampler_ram_audio_projection_contract.h"
#include "IPC/sampler_ram_audio_projection_control.h"
#include "IPC/shared_memory_ref_control.h"

#include <string.h>

#include "Platform/cache_maintenance.h"
#include "Platform/memory_layout.h"
#include "Sampler/sample_global_pool.h"
#include "stm32h7xx_hal.h"

void sampler_ram_audio_projection_init(void)
{
    memset(g_sampler_ram_audio_slots, 0, sizeof(g_sampler_ram_audio_slots));
    for (uint16_t i = 0U; i < SAMPLER_RAM_AUDIO_SLOT_COUNT; ++i)
        g_sampler_ram_audio_global_to_slot[i] = SAMPLER_RAM_POOL_INVALID_SLOT;
    __DMB();
}

uint8_t sampler_ram_audio_projection_publish(uint16_t ram_slot,
                                             const sampler_ram_slot_t *slot)
{
    if ((ram_slot >= SAMPLER_RAM_AUDIO_SLOT_COUNT) || (slot == NULL)
        || (slot->state != SAMPLER_RAM_SLOT_READY) || (slot->data == NULL)
        || (slot->frames == 0U)) return 0U;
    sampler_ram_audio_slot_t *const dst = &g_sampler_ram_audio_slots[ram_slot];
    dst->ready = 0U;
    __DMB();
    audio_shared_memory_ref_t data;
    const uint32_t data_bytes = slot->frames * slot->bytes_per_frame;
    if (shared_memory_ref_make_page_pool(slot->first_page_slot, 0U,
                                         data_bytes, &data) == 0U) return 0U;
    dcache_clean_by_addr_aligned(slot->data, data_bytes);
    dcache_invalidate_by_addr_aligned(slot->data, data_bytes);
    dst->descriptor = (sampler_ram_audio_descriptor_t){
        .generation = slot->generation, .frames = slot->frames,
        .sample_rate = slot->sample_rate, .data_offset = slot->data_offset,
        .data = data, .global_slot = slot->global_slot, .ram_slot = ram_slot,
        .channels = slot->channels, .bytes_per_frame = slot->bytes_per_frame,
        .format = (uint8_t)slot->format
    };
    dst->sequence++;
    __DMB();
    dst->ready = 1U;
    __DMB();
    if (slot->global_slot < SAMPLER_RAM_AUDIO_SLOT_COUNT)
        g_sampler_ram_audio_global_to_slot[slot->global_slot] = ram_slot;
    return 1U;
}

void sampler_ram_audio_projection_withdraw(uint16_t ram_slot, uint32_t generation)
{
    if (ram_slot >= SAMPLER_RAM_AUDIO_SLOT_COUNT) return;
    sampler_ram_audio_slot_t *const slot = &g_sampler_ram_audio_slots[ram_slot];
    if ((generation != 0U) && (slot->descriptor.generation != generation)) return;
    if ((slot->descriptor.global_slot < SAMPLER_RAM_AUDIO_SLOT_COUNT)
        && (g_sampler_ram_audio_global_to_slot[slot->descriptor.global_slot]
            == ram_slot))
        g_sampler_ram_audio_global_to_slot[slot->descriptor.global_slot] =
            SAMPLER_RAM_POOL_INVALID_SLOT;
    slot->ready = 0U;
    __DMB();
    slot->sequence++;
    __DMB();
}
