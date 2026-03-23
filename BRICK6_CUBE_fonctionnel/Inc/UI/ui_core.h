#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_TRACK_COUNT 8U
#define UI_AUDIO_INPUT_RESOURCE_COUNT 4U

typedef enum
{
    UI_TRACK_FAMILY_INPUT1 = 0,
    UI_TRACK_FAMILY_INPUT2,
    UI_TRACK_FAMILY_INPUT3,
    UI_TRACK_FAMILY_INPUT4,
    UI_TRACK_FAMILY_SYNTH,
    UI_TRACK_FAMILY_COUNT
} ui_track_family_t;

typedef enum
{
    UI_TRACK_TYPE_AUDIO = 0,
    UI_TRACK_TYPE_HYBRID,
    UI_TRACK_TYPE_DX7,
    UI_TRACK_TYPE_COUNT
} ui_track_type_t;

typedef struct
{
    ui_track_family_t family;
    ui_track_type_t type;
} ui_track_config_t;

void ui_core_init(void);
void ui_core_tick(void);
void ui_core_service_track_selection_inputs(void);

uint8_t ui_get_active_track(void);
ui_track_config_t ui_get_track_config(uint8_t track);
ui_track_family_t ui_get_track_family(uint8_t track);
ui_track_type_t ui_get_track_type(uint8_t track);
bool ui_set_track_family(uint8_t track, ui_track_family_t family);
bool ui_set_track_type(uint8_t track, ui_track_type_t type);
bool ui_track_family_is_input(ui_track_family_t family);
bool ui_track_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type);
ui_track_type_t ui_get_default_track_type_for_family(ui_track_family_t family);
uint8_t ui_get_track_type_count_for_family(ui_track_family_t family);
uint8_t ui_get_track_type_index_for_family(ui_track_family_t family, ui_track_type_t type);
ui_track_type_t ui_get_track_type_from_family_index(ui_track_family_t family, uint8_t index);
uint8_t ui_count_tracks_with_family(ui_track_family_t family);
const char *ui_get_track_family_display_name(ui_track_family_t family);
const char *ui_get_track_family_short_name(ui_track_family_t family);
const char *ui_get_track_type_display_name(ui_track_family_t family, ui_track_type_t type);
const char *ui_get_track_type_short_name(ui_track_family_t family, ui_track_type_t type);
void ui_get_track_runtime_header_label(uint8_t track, char *out, uint32_t out_len);
uint8_t ui_core_hall_note_is_suppressed(uint8_t hall);
void ui_core_clear_hall_note_suppression(uint8_t hall);

#endif /* UI_CORE_H */
