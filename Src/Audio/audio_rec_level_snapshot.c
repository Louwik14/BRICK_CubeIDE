#include "Audio/audio_rec_level_snapshot.h"

#include "Storage/memory_layout.h"
#include "stm32h7xx.h"

typedef struct
{
    volatile uint32_t sequence;
    volatile uint32_t generation;
    volatile uint32_t peak_abs_pcm24;
} audio_rec_level_mailbox_t;

_Static_assert(sizeof(audio_rec_level_mailbox_t) == 12U,
               "AUDIO REC level mailbox ABI changed");

D3_IPC static audio_rec_level_mailbox_t g_audio_rec_level_mailbox;

void audio_rec_level_snapshot_audio_init(void)
{
    g_audio_rec_level_mailbox.sequence = 0U;
    g_audio_rec_level_mailbox.generation = 0U;
    g_audio_rec_level_mailbox.peak_abs_pcm24 = 0U;
    __DMB();
}

void audio_rec_level_snapshot_audio_publish(uint32_t peak_abs_pcm24)
{
    uint32_t sequence = g_audio_rec_level_mailbox.sequence;
    if((sequence & 1U) != 0U)
    {
        ++sequence;
    }
    uint32_t generation = g_audio_rec_level_mailbox.generation + 1U;
    if(generation == 0U)
    {
        generation = 1U;
    }

    g_audio_rec_level_mailbox.sequence = sequence + 1U;
    __DMB();
    g_audio_rec_level_mailbox.generation = generation;
    g_audio_rec_level_mailbox.peak_abs_pcm24 = peak_abs_pcm24;
    __DMB();
    g_audio_rec_level_mailbox.sequence = sequence + 2U;
    if(g_audio_rec_level_mailbox.sequence == 0U)
    {
        g_audio_rec_level_mailbox.sequence = 2U;
    }
    __DMB();
}

uint8_t audio_rec_level_snapshot_control_read(
    audio_rec_level_snapshot_t *out_snapshot)
{
    if(out_snapshot == 0)
    {
        return 0U;
    }

    for(uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_audio_rec_level_mailbox.sequence;
        if((before == 0U) || ((before & 1U) != 0U))
        {
            continue;
        }
        __DMB();
        audio_rec_level_snapshot_t next = {
            .generation = g_audio_rec_level_mailbox.generation,
            .peak_abs_pcm24 = g_audio_rec_level_mailbox.peak_abs_pcm24
        };
        __DMB();
        const uint32_t after = g_audio_rec_level_mailbox.sequence;
        if((before == after) && ((after & 1U) == 0U))
        {
            *out_snapshot = next;
            return (uint8_t)(next.generation != 0U);
        }
    }
    return 0U;
}
