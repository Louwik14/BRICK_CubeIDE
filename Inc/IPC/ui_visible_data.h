#ifndef BRICK6_UI_VISIBLE_DATA_H
#define BRICK6_UI_VISIBLE_DATA_H

#include <stdint.h>

typedef enum
{
    UI_VISIBLE_DATA_CPU_LOAD = 0,
    UI_VISIBLE_DATA_PARAMETER,
    UI_VISIBLE_DATA_SYNTH_WAVEFORM,
    UI_VISIBLE_DATA_AUDIO_WAVEFORM,
    UI_VISIBLE_DATA_COUNT
} ui_visible_data_kind_t;

/* Presentation doorbell only. Payloads remain in their owning snapshots. */
void ui_visible_data_notify(ui_visible_data_kind_t kind, uint32_t version);
void ui_visible_data_cpu_set_visible(uint8_t visible);

#endif /* BRICK6_UI_VISIBLE_DATA_H */
