#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdint.h>

#define UI_TRACK_COUNT 8U

typedef enum
{
    UI_TRACK_TYPE_AUDIO = 0,
    UI_TRACK_TYPE_SYNTH,
    UI_TRACK_TYPE_MIDI,
    UI_TRACK_TYPE_CARD,
    UI_TRACK_TYPE_COUNT
} ui_track_type_t;

void ui_core_init(void);
void ui_core_tick(void);
void ui_core_service_track_selection_inputs(void);

uint8_t ui_get_active_track(void);
ui_track_type_t ui_get_track_type(uint8_t track);
uint8_t ui_core_hall_note_is_suppressed(uint8_t hall);
void ui_core_clear_hall_note_suppression(uint8_t hall);

uint8_t ui_get_active_track(void);
ui_track_type_t ui_get_track_type(uint8_t track);

#endif /* UI_CORE_H */
