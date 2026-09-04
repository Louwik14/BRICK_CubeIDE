#include "IPC/sampler_ram_audio_projection_contract.h"
#include "Audio/sampler_ram_audio_projection_audio.h"

#include <string.h>

#include "stm32h7xx_hal.h"

uint8_t sampler_ram_audio_projection_resolve(uint16_t global_slot,
                                             sampler_ram_audio_descriptor_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (global_slot >= SAMPLER_RAM_AUDIO_SLOT_COUNT)) return 0U;
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint16_t i = g_sampler_ram_audio_global_to_slot[global_slot];
        __DMB();
        if (i >= SAMPLER_RAM_AUDIO_SLOT_COUNT) return 0U;
        const sampler_ram_audio_slot_t *const slot = &g_sampler_ram_audio_slots[i];
        const uint32_t before = slot->sequence;
        if ((before & 1U) != 0U) continue;
        __DMB();
        const sampler_ram_audio_slot_t snap = *slot;
        __DMB();
        if ((before == slot->sequence) && ((before & 1U) == 0U)
            && (snap.ready != 0U)
            && (snap.descriptor.global_slot == global_slot))
        {
            *out = snap.descriptor;
            return 1U;
        }
    }
    return 0U;
}
