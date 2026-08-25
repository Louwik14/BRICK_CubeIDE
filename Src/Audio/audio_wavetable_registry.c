#include "Audio/audio_wavetable_registry.h"

#include <string.h>

#include "Sampler/wavetable_pool.h"
#include "Storage/cache_maintenance.h"
#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint8_t ready;
    uint8_t reserved[3];
    audio_wavetable_descriptor_t descriptor;
} audio_wavetable_registry_slot_t;

AUDIO_COLD_SDRAM static audio_wavetable_registry_slot_t
    g_audio_wavetable_registry[WAVETABLE_POOL_MAX_SLOTS];

void audio_wavetable_registry_init(void)
{
    memset(g_audio_wavetable_registry, 0, sizeof(g_audio_wavetable_registry));
}

uint8_t audio_wavetable_registry_transport_install(
    const audio_wavetable_descriptor_t *descriptor)
{
    if ((descriptor == NULL)
        || (descriptor->wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)
        || (descriptor->generation == 0U)) return 0U;
    const void *base = audio_shared_memory_resolve(&descriptor->base_data);
    if (base == NULL) return 0U;
    dcache_invalidate_by_addr_aligned(base, descriptor->base_data.length);
    for (uint16_t i = 0U; i < descriptor->band_count; ++i)
    {
        const void *band = audio_shared_memory_resolve(&descriptor->bands[i].data);
        if (band == NULL) return 0U;
        dcache_invalidate_by_addr_aligned(band, descriptor->bands[i].data.length);
    }
    audio_wavetable_registry_slot_t *const dst =
        &g_audio_wavetable_registry[descriptor->wavetable_slot];
    dst->ready = 0U;
    __DMB();
    dst->descriptor = *descriptor;
    dst->sequence++;
    __DMB();
    dst->ready = 1U;
    return 1U;
}

uint8_t audio_wavetable_registry_resolve(uint16_t wavetable_slot,
                                         uint32_t generation,
                                         audio_wavetable_descriptor_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)) return 0U;
    const audio_wavetable_registry_slot_t snap = g_audio_wavetable_registry[wavetable_slot];
    __DMB();
    if ((snap.ready == 0U)
        || ((generation != 0U) && (snap.descriptor.generation != generation))
        || (snap.sequence != g_audio_wavetable_registry[wavetable_slot].sequence)
        || (g_audio_wavetable_registry[wavetable_slot].ready == 0U)) return 0U;
    *out = snap.descriptor;
    return 1U;
}

uint8_t audio_wavetable_registry_resolve_global(uint16_t global_slot,
                                                audio_wavetable_descriptor_t *out)
{
    for (uint16_t i = 0U; i < WAVETABLE_POOL_MAX_SLOTS; ++i)
    {
        const audio_wavetable_registry_slot_t snap = g_audio_wavetable_registry[i];
        __DMB();
        if ((snap.ready != 0U) && (snap.descriptor.global_slot == global_slot)
            && (snap.sequence == g_audio_wavetable_registry[i].sequence)
            && (g_audio_wavetable_registry[i].ready != 0U))
        { if (out != NULL) *out = snap.descriptor; return (out != NULL); }
    }
    return 0U;
}

void audio_wavetable_registry_remove(uint16_t wavetable_slot,
                                     uint32_t generation)
{
    if (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS) return;
    audio_wavetable_registry_slot_t *const slot = &g_audio_wavetable_registry[wavetable_slot];
    if ((generation != 0U) && (slot->descriptor.generation != generation)) return;
    slot->ready = 0U;
    __DMB();
    slot->sequence++;
}
