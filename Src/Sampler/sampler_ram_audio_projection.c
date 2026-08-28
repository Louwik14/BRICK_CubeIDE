#include "Sampler/sampler_ram_audio_projection.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Audio/audio_shared_memory.h"
#include "Storage/cache_maintenance.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t ready;
    uint8_t reserved[3];
    sampler_ram_audio_descriptor_t descriptor;
} sampler_ram_audio_slot_t;

AUDIO_SHARED_REGISTRY_SDRAM static sampler_ram_audio_slot_t
    g_ram_audio_slots[SAMPLER_RAM_POOL_MAX_SLOTS];
D2_IPC static volatile uint16_t
    g_ram_audio_global_to_slot[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];

void sampler_ram_audio_projection_init(void)
{
    memset(g_ram_audio_slots, 0, sizeof(g_ram_audio_slots));
    for (uint16_t i = 0U; i < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS; ++i)
        g_ram_audio_global_to_slot[i] = SAMPLER_RAM_POOL_INVALID_SLOT;
    __DMB();
}

uint8_t sampler_ram_audio_projection_publish(uint16_t ram_slot,
                                             const sampler_ram_slot_t *slot)
{
    if ((ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS) || (slot == NULL)
        || (slot->state != SAMPLER_RAM_SLOT_READY) || (slot->data == NULL)
        || (slot->frames == 0U)) return 0U;
    sampler_ram_audio_slot_t *const dst = &g_ram_audio_slots[ram_slot];
    dst->ready = 0U;
    __DMB();
    audio_shared_memory_ref_t data;
    const uint32_t data_bytes = slot->frames * slot->bytes_per_frame;
    if (audio_shared_memory_page_ref(slot->first_page_slot, 0U,
                                     data_bytes, &data) == 0U) return 0U;
    dcache_clean_by_addr_aligned(slot->data, data_bytes);
    dcache_invalidate_by_addr_aligned(slot->data, data_bytes);
    dst->descriptor = (sampler_ram_audio_descriptor_t){
        .generation = slot->generation, .frames = slot->frames,
        .sample_rate = slot->sample_rate, .data_offset = slot->data_offset,
        .data = data, .global_slot = slot->global_slot,
        .ram_slot = ram_slot, .channels = slot->channels,
        .bytes_per_frame = slot->bytes_per_frame, .format = slot->format
    };
    dst->sequence++;
    __DMB();
    dst->ready = 1U;
    __DMB();
    __DMB();
    if (slot->global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        g_ram_audio_global_to_slot[slot->global_slot] = ram_slot;
    return 1U;
}

void sampler_ram_audio_projection_withdraw(uint16_t ram_slot, uint32_t generation)
{
    if (ram_slot >= SAMPLER_RAM_POOL_MAX_SLOTS) return;
    sampler_ram_audio_slot_t *const slot = &g_ram_audio_slots[ram_slot];
    if ((generation != 0U) && (slot->descriptor.generation != generation)) return;
    if ((slot->descriptor.global_slot < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        && (g_ram_audio_global_to_slot[slot->descriptor.global_slot] == ram_slot))
        g_ram_audio_global_to_slot[slot->descriptor.global_slot] =
            SAMPLER_RAM_POOL_INVALID_SLOT;
    slot->ready = 0U;
    __DMB();
    slot->sequence++;
    __DMB();
}

uint8_t sampler_ram_audio_projection_resolve(uint16_t global_slot,
                                             sampler_ram_audio_descriptor_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (global_slot >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))
        return 0U;
    const uint16_t i = g_ram_audio_global_to_slot[global_slot];
    __DMB();
    if (i >= SAMPLER_RAM_POOL_MAX_SLOTS) return 0U;
    __DMB();
    const sampler_ram_audio_slot_t snap = g_ram_audio_slots[i];
    __DMB();
    if ((snap.ready == 0U) || (snap.descriptor.global_slot != global_slot)
        || (snap.sequence != g_ram_audio_slots[i].sequence)
        || (g_ram_audio_slots[i].ready == 0U)) return 0U;
    *out = snap.descriptor;
    return 1U;
}
