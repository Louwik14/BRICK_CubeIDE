#include "Audio/audio_wavetable_registry.h"
#include "IPC/audio_wavetable_registry_contract.h"

#include <string.h>

#include "Platform/intercore_cache.h"
#include "stm32h7xx.h"

uint8_t audio_wavetable_registry_resolve(uint16_t wavetable_slot,
                                         uint32_t generation,
                                         audio_wavetable_descriptor_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (wavetable_slot >= WAVETABLE_POOL_MAX_SLOTS)) return 0U;
    const audio_wavetable_registry_slot_t *const src =
        &g_audio_wavetable_registry[wavetable_slot];
    intercore_cache_consume((const void *)&src->active_snapshot,
                            sizeof(src->active_snapshot));
    const uint32_t active = src->active_snapshot;
    const audio_wavetable_registry_snapshot_t *const published =
        &src->snapshots[active];
    intercore_cache_consume(published, sizeof(*published));
    const audio_wavetable_registry_snapshot_t snap = *published;
    if ((snap.ready == 0U)
        || ((generation != 0U) && (snap.descriptor.generation != generation)))
        return 0U;
    *out = snap.descriptor;
    return 1U;
}
