#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_TRACK_COUNT 8U
#define UI_AUDIO_TRACK_LIMIT 4U


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
bool ui_set_track_type(uint8_t track, ui_track_type_t type);
uint8_t ui_count_tracks_of_type(ui_track_type_t type);
const char *ui_get_track_type_display_name(ui_track_type_t type);
const char *ui_get_track_type_short_name(ui_track_type_t type);
uint8_t ui_core_hall_note_is_suppressed(uint8_t hall);
void ui_core_clear_hall_note_suppression(uint8_t hall);

#endif /* UI_CORE_H */
