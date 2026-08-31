#include "IPC/sampler_ram_audio_projection_contract.h"
#include "Audio/sampler_ram_audio_projection_audio.h"

#include <string.h>

#include "stm32h7xx_hal.h"

uint8_t sampler_ram_audio_projection_resolve(uint16_t global_slot,
                                             sampler_ram_audio_descriptor_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (global_slot >= SAMPLER_RAM_AUDIO_SLOT_COUNT)) return 0U;
    const uint16_t i = g_sampler_ram_audio_global_to_slot[global_slot];
    __DMB();
    if (i >= SAMPLER_RAM_AUDIO_SLOT_COUNT) return 0U;
    __DMB();
    const sampler_ram_audio_slot_t snap = g_sampler_ram_audio_slots[i];
    __DMB();
    if ((snap.ready == 0U) || (snap.descriptor.global_slot != global_slot)
        || (snap.sequence != g_sampler_ram_audio_slots[i].sequence)
        || (g_sampler_ram_audio_slots[i].ready == 0U)) return 0U;
    *out = snap.descriptor;
    return 1U;
}
