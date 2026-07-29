#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_TRACK_COUNT 14U
#if defined(BRICK6_VARIANT_LOWCOST)
#define UI_AUDIO_INPUT_RESOURCE_COUNT 1U
#define UI_AUDIO_INPUT_PROTO_WIRED_COUNT 1U
#else
#define UI_AUDIO_INPUT_RESOURCE_COUNT 4U
#define UI_AUDIO_INPUT_PROTO_WIRED_COUNT 3U
#endif

typedef enum
{
    UI_TRACK_FAMILY_OFF = 0,
    UI_TRACK_FAMILY_INPUT1,
    UI_TRACK_FAMILY_INPUT2,
    UI_TRACK_FAMILY_INPUT3,
    UI_TRACK_FAMILY_INPUT4,
    UI_TRACK_FAMILY_SYNTH,
    UI_TRACK_FAMILY_DRUM,
    UI_TRACK_FAMILY_MASTER,
    UI_TRACK_FAMILY_MIDI,
    UI_TRACK_FAMILY_SAMPLER,
    UI_TRACK_FAMILY_COUNT
} ui_track_family_t;

typedef enum
{
    UI_TRACK_TYPE_AUDIO = 0,
    UI_TRACK_TYPE_HYBRID,
    UI_TRACK_TYPE_RAM,
    UI_TRACK_TYPE_PRISM,
    UI_TRACK_TYPE_DRUM_TRX_BD,
    UI_TRACK_TYPE_MIDI,
    UI_TRACK_TYPE_STREAM,
    UI_TRACK_TYPE_MASTER_FX,
    UI_TRACK_TYPE_DRUM_BD_ANALOG,
    UI_TRACK_TYPE_LOOPER,
    UI_TRACK_TYPE_MULTI,
    UI_TRACK_TYPE_STACK,
    UI_TRACK_TYPE_WAVE,
    UI_TRACK_TYPE_DELUGE,
    UI_TRACK_TYPE_COUNT
} ui_track_type_t;

typedef struct
{
    ui_track_family_t family;
    ui_track_type_t type;
} ui_track_config_t;

typedef enum
{
    UI_HALL_MODE_SEQ = 0,
    UI_HALL_MODE_KEYBOARD,
    UI_HALL_MODE_ARP,
    UI_HALL_MODE_MACRO,
    UI_HALL_MODE_AUDIO_REC,
    UI_HALL_MODE_PATCH,
    UI_HALL_MODE_PATTERN,
    UI_HALL_MODE_MUTE,
    UI_HALL_MODE_COUNT
} ui_hall_mode_t;

typedef enum
{
    UI_HALL_MODE_VIEW_SEQ = 0,
    UI_HALL_MODE_VIEW_KEYBOARD,
    UI_HALL_MODE_VIEW_ARP,
    UI_HALL_MODE_VIEW_ROUT,
    UI_HALL_MODE_VIEW_MACRO,
    UI_HALL_MODE_VIEW_AUDIO_REC,
    UI_HALL_MODE_VIEW_PATCH,
    UI_HALL_MODE_VIEW_PATTERN,
    UI_HALL_MODE_VIEW_MUTE,
    UI_HALL_MODE_VIEW_COUNT
} ui_hall_mode_effective_view_t;

typedef enum
{
    UI_MUTE_SUBMODE_NONE = 0,
    UI_MUTE_SUBMODE_QUICK,
    UI_MUTE_SUBMODE_HOLD_QUICK,
    UI_MUTE_SUBMODE_PREPARE
} ui_mute_submode_t;

typedef struct
{
    uint8_t active;
    ui_mute_submode_t submode;
} ui_mute_state_t;

typedef struct
{
    uint8_t visible;
    uint8_t blink;
    uint8_t muted;
} ui_mute_hall_led_t;

typedef enum
{
    UI_PATTERN_SUBSTATE_BANK_SELECT = 0,
    UI_PATTERN_SUBSTATE_PATTERN_SELECT
} ui_pattern_substate_t;

typedef enum
{
    UI_PATTERN_MODE_RECALL = 0,
    UI_PATTERN_MODE_STORE
} ui_pattern_mode_t;

typedef enum
{
    UI_MACRO_OVERLAY_SUBMODE_CTRL = 0,
    UI_MACRO_OVERLAY_SUBMODE_ASSIGN,
    UI_MACRO_OVERLAY_SUBMODE_COUNT
} ui_macro_overlay_submode_t;

typedef struct
{
    uint8_t active_bank;
    uint8_t active_pattern;
    uint8_t queued_valid;
    uint8_t queued_bank;
    uint8_t queued_pattern;
    ui_pattern_substate_t substate;
    uint8_t selected_bank;
    ui_pattern_mode_t mode;
} ui_pattern_stub_state_t;

typedef enum
{
    UI_TRACK_MIDI_SRC_INT = 0,
    UI_TRACK_MIDI_SRC_EXT,
    UI_TRACK_MIDI_SRC_ALL,
    UI_TRACK_MIDI_SRC_COUNT
} ui_track_midi_source_t;

#include "ui_hall_mode_projection.h"
#include "ui_hall_mode_state.h"

void ui_core_init(void);
void ui_core_tick(void);
void ui_core_service_track_selection_inputs(void);

uint8_t ui_get_active_track(void);
bool ui_resolve_filter_target_track(uint8_t *out_track_id);
ui_track_config_t ui_get_track_config(uint8_t track);
ui_track_family_t ui_get_track_family(uint8_t track);
ui_track_type_t ui_get_track_type(uint8_t track);
bool ui_set_track_family(uint8_t track, ui_track_family_t family);
bool ui_set_track_type(uint8_t track, ui_track_type_t type);
bool ui_track_family_is_input(ui_track_family_t family);
bool ui_track_family_is_engine(ui_track_family_t family);
bool ui_track_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type);
bool ui_track_type_is_available(uint8_t track, ui_track_family_t family, ui_track_type_t type);
ui_track_type_t ui_get_default_track_type_for_family(ui_track_family_t family);
uint8_t ui_get_track_type_count_for_family(ui_track_family_t family);
uint8_t ui_get_track_type_index_for_family(ui_track_family_t family, ui_track_type_t type);
ui_track_type_t ui_get_track_type_from_family_index(ui_track_family_t family, uint8_t index);
uint8_t ui_count_tracks_with_family(ui_track_family_t family);
const char *ui_get_track_family_display_name(ui_track_family_t family);
const char *ui_get_track_family_short_name(ui_track_family_t family);
const char *ui_get_track_type_display_name(ui_track_family_t family, ui_track_type_t type);
const char *ui_get_track_type_short_name(ui_track_family_t family, ui_track_type_t type);
uint8_t ui_get_track_midi_channel(uint8_t track);
bool ui_set_track_midi_channel(uint8_t track, uint8_t channel_1_16);
ui_track_midi_source_t ui_get_track_midi_source(uint8_t track);
bool ui_set_track_midi_source(uint8_t track, ui_track_midi_source_t source);
bool ui_apply_track_config_bulk_mutation(const uint8_t family[UI_TRACK_COUNT],
                                         const uint8_t type[UI_TRACK_COUNT],
                                         const uint8_t midi_channel[UI_TRACK_COUNT],
                                         const uint8_t midi_source[UI_TRACK_COUNT]);
bool ui_restore_track_config_bulk(const uint8_t family[UI_TRACK_COUNT],
                                  const uint8_t type[UI_TRACK_COUNT],
                                  const uint8_t midi_channel[UI_TRACK_COUNT],
                                  const uint8_t midi_source[UI_TRACK_COUNT]);
uint8_t ui_track_midi_channel_used_by_other(uint8_t track, uint8_t channel_1_16);
void ui_get_track_runtime_header_label(uint8_t track, char *out, uint32_t out_len);

ui_mute_state_t ui_get_mute_state(void);
uint8_t ui_get_mute_hall_led(uint8_t hall, ui_mute_hall_led_t *out_led);
void ui_get_pattern_stub_state(ui_pattern_stub_state_t *out_state);
uint8_t ui_core_request_undo(void);
uint8_t ui_is_track_modifier_held(void);
uint8_t ui_macro_overlay_is_active(void);
uint8_t ui_macro_overlay_is_latched(void);
uint8_t ui_macro_overlay_get_submode(ui_macro_overlay_submode_t *out_submode);
void ui_macro_overlay_on_hall_mode_changed(void);
uint8_t ui_core_hall_note_is_suppressed(uint8_t hall);
void ui_core_clear_hall_note_suppression(uint8_t hall);

#endif /* UI_CORE_H */
