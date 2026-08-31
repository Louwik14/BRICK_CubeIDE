#include "Audio/audio_rec_bus_runtime.h"

static audio_rec_bus_runtime_t g_audio_rec_bus;

void audio_rec_bus_runtime_init(void)
{ g_audio_rec_bus=(audio_rec_bus_runtime_t){0}; }

uint8_t audio_rec_bus_runtime_apply(uint32_t packed)
{
    g_audio_rec_bus.source_entity_mask=(uint16_t)packed;
    g_audio_rec_bus.arm=(uint8_t)((packed>>16)&3U);
    g_audio_rec_bus.source_flags=(uint8_t)((packed>>18)&7U);
    return 1U;
}

const audio_rec_bus_runtime_t *audio_rec_bus_runtime_get(void)
{ return &g_audio_rec_bus; }
