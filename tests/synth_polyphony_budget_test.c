#include <stdint.h>

#include "Core/synth_polyphony.h"

static int unique_slots(void)
{
    uint8_t seen[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET] = { 0U };
    for (uint8_t track = 0U; track < SYNTH_POLYPHONY_TRACK_CAPACITY; ++track)
    {
        if (synth_polyphony_get_track_active(track) == 0U) continue;
        const uint8_t count = synth_polyphony_get_voice_count(track);
        for (uint8_t voice = 0U; voice < count; ++voice)
        {
            const uint8_t slot = synth_polyphony_get_slot(track, voice);
            if ((slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) || (seen[slot] != 0U)) return 0;
            seen[slot] = 1U;
        }
    }
    return 1;
}

int main(void)
{
    synth_polyphony_init();
    if (synth_polyphony_get_free_count() != 16U) return 1;
    if (synth_polyphony_set_track_active(0U, 1U, 1U) == 0U) return 2;
    if (synth_polyphony_set_voice_count(0U, 8U) != 8U) return 3;
    if (synth_polyphony_set_track_active(1U, 1U, 2U) == 0U) return 4;
    if (synth_polyphony_set_voice_count(1U, 8U) != 8U) return 5;
    if ((synth_polyphony_get_free_count() != 0U) || (unique_slots() == 0)) return 6;
    if (synth_polyphony_set_track_active(2U, 1U, 3U) != 0U) return 7;
    if (synth_polyphony_set_voice_count(0U, 4U) != 4U) return 8;
    if (synth_polyphony_get_free_count() != 4U) return 9;
    if (synth_polyphony_set_track_active(2U, 1U, 3U) == 0U) return 10;
    if (synth_polyphony_set_voice_count(2U, 8U) != 3U) return 11;
    if ((synth_polyphony_get_free_count() != 0U) || (unique_slots() == 0)) return 12;
    (void)synth_polyphony_set_track_active(1U, 0U, 0U);
    if (synth_polyphony_get_free_count() != 8U) return 13;
    return 0;
}
