#include "IPC/audio_rec_level_reader.h"
#include "stm32h7xx.h"

uint8_t audio_rec_level_reader_read(audio_rec_level_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0) return 0U;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt)
    {
        const uint32_t before = g_audio_rec_level_layout.sequence;
        if ((before == 0U) || ((before & 1U) != 0U)) continue;
        __DMB();
        audio_rec_level_snapshot_t next = {
            .generation = g_audio_rec_level_layout.generation,
            .peak_abs_pcm24 = g_audio_rec_level_layout.peak_abs_pcm24
        };
        __DMB();
        const uint32_t after = g_audio_rec_level_layout.sequence;
        if ((before == after) && ((after & 1U) == 0U))
        {
            *out_snapshot = next;
            return (uint8_t)(next.generation != 0U);
        }
    }
    return 0U;
}
