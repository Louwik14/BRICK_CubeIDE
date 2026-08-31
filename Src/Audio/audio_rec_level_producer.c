#include "Audio/audio_rec_level_producer.h"
#include "IPC/audio_rec_level_contract.h"
#include "stm32h7xx.h"

void audio_rec_level_producer_init(void)
{
    g_audio_rec_level_layout.sequence = 0U;
    g_audio_rec_level_layout.generation = 0U;
    g_audio_rec_level_layout.peak_abs_pcm24 = 0U;
    __DMB();
}

void audio_rec_level_producer_publish(uint32_t peak_abs_pcm24)
{
    uint32_t sequence = g_audio_rec_level_layout.sequence;
    if ((sequence & 1U) != 0U) ++sequence;
    uint32_t generation = g_audio_rec_level_layout.generation + 1U;
    if (generation == 0U) generation = 1U;
    g_audio_rec_level_layout.sequence = sequence + 1U;
    __DMB();
    g_audio_rec_level_layout.generation = generation;
    g_audio_rec_level_layout.peak_abs_pcm24 = peak_abs_pcm24;
    __DMB();
    g_audio_rec_level_layout.sequence = sequence + 2U;
    if (g_audio_rec_level_layout.sequence == 0U) g_audio_rec_level_layout.sequence = 2U;
    __DMB();
}
