#ifndef AUDIO_WAVE_TABLE_PROJECTION_H
#define AUDIO_WAVE_TABLE_PROJECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t wavetable_slot;
    uint16_t reserved;
    uint32_t generation;
} audio_wave_table_selection_t;

_Static_assert(sizeof(audio_wave_table_selection_t) == 8U,
               "AUDIO wavetable selection must remain compact");

void audio_wave_table_projection_init(void);
uint8_t audio_wave_table_projection_publish_track(
    uint8_t track, uint8_t osc, uint16_t logical_slot);
void audio_wave_table_projection_withdraw_slot(uint16_t wavetable_slot,
                                               uint32_t generation);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_WAVE_TABLE_PROJECTION_H */
