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
    audio_wavetable_registry_slot_t snap = {0};
    uint8_t stable = 0U;
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        intercore_cache_consume(src, sizeof(*src));
        const uint32_t before = src->sequence;
        if ((before & 1U) != 0U) continue;
        snap = *src;
        intercore_cache_consume(src, sizeof(*src));
        if ((before == src->sequence) && ((before & 1U) == 0U))
        {
            stable = 1U;
            break;
        }
    }
    if ((stable == 0U) || (snap.ready == 0U)
        || ((generation != 0U) && (snap.descriptor.generation != generation)))
        return 0U;
    *out = snap.descriptor;
    return 1U;
}
