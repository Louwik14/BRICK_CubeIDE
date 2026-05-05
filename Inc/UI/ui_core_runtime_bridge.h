#ifndef UI_CORE_RUNTIME_BRIDGE_H
#define UI_CORE_RUNTIME_BRIDGE_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

typedef void (*ui_core_runtime_bridge_feedback_fn)(const char *message);
typedef void (*ui_core_runtime_bridge_set_hall_mode_fn)(ui_hall_mode_t mode);
typedef void (*ui_core_runtime_bridge_pattern_enter_fn)(ui_pattern_mode_t mode);
typedef uint8_t (*ui_core_runtime_bridge_undo_request_fn)(void);
typedef void (*ui_core_runtime_bridge_suppress_hall_note_fn)(uint8_t hall);
typedef void (*ui_core_runtime_bridge_post_sync_fn)(uint8_t sync_active_track_ui_context);

bool ui_core_runtime_bridge_apply_track_family_change(uint8_t track,
                                                      ui_track_family_t family,
                                                      uint8_t active_track_touched,
                                                      ui_core_runtime_bridge_post_sync_fn post_sync);

bool ui_core_runtime_bridge_apply_track_type_change(uint8_t track,
                                                     ui_track_type_t type,
                                                     uint8_t active_track_touched,
                                                     ui_core_runtime_bridge_post_sync_fn post_sync);

bool ui_core_runtime_bridge_restore_track_config_bulk(const uint8_t family[UI_TRACK_COUNT],
                                                      const uint8_t type[UI_TRACK_COUNT],
                                                      const uint8_t midi_channel[UI_TRACK_COUNT],
                                                      const uint8_t midi_source[UI_TRACK_COUNT],
                                                      ui_core_runtime_bridge_post_sync_fn post_sync);

uint8_t ui_core_runtime_bridge_handle_master_buffer_routing_event(const ui_event_t *ev,
                                                                   uint8_t active_track,
                                                                   ui_hall_mode_t hall_mode,
                                                                   uint8_t track_select_armed,
                                                                   ui_core_runtime_bridge_suppress_hall_note_fn suppress_hall_note);
uint8_t ui_core_runtime_bridge_get_master_fx_route_enabled(uint8_t track);

uint8_t ui_core_runtime_bridge_handle_transport_event(const ui_event_t *ev,
                                                      uint8_t mute_active,
                                                      uint8_t shift_down,
                                                      uint8_t track_select_armed,
                                                      ui_core_runtime_bridge_pattern_enter_fn pattern_enter,
                                                      ui_core_runtime_bridge_feedback_fn feedback);

uint8_t ui_core_runtime_bridge_request_undo(ui_core_runtime_bridge_feedback_fn feedback);

uint8_t ui_core_runtime_bridge_handle_pattern_mode_event(const ui_event_t *ev,
                                                         ui_hall_mode_t hall_mode,
                                                         uint8_t shift_down,
                                                         uint8_t track_select_armed,
                                                         ui_core_runtime_bridge_set_hall_mode_fn set_hall_mode,
                                                         ui_core_runtime_bridge_feedback_fn feedback);

uint8_t ui_core_runtime_bridge_handle_global_shortcuts(const ui_event_t *ev,
                                                       uint8_t shift_down,
                                                       uint8_t track_select_armed,
                                                       uint8_t mute_active,
                                                       ui_core_runtime_bridge_undo_request_fn undo_request,
                                                       ui_core_runtime_bridge_feedback_fn feedback);

uint8_t ui_core_runtime_bridge_handle_seq_mode_event(const ui_event_t *ev,
                                                     ui_hall_mode_t hall_mode,
                                                     uint8_t shift_down,
                                                     ui_core_runtime_bridge_feedback_fn feedback);

void ui_core_runtime_bridge_step_octave(int8_t step);
void ui_core_runtime_bridge_notify_hall_mode_changed(ui_hall_mode_t previous_mode,
                                                     ui_hall_mode_t next_mode);
void ui_core_runtime_bridge_update_seq_step_hold(void);
uint8_t ui_core_runtime_bridge_resolve_filter_target_track(uint8_t *out_track_id);
uint8_t ui_core_runtime_bridge_get_seq_edit_page(uint8_t track);
int8_t ui_core_runtime_bridge_get_keyboard_octave_shift(void);
void ui_core_runtime_bridge_get_pattern_stub_state(ui_pattern_stub_state_t *out_state);
void ui_core_runtime_bridge_sync_active_track_context(uint8_t include_keyboard_focus_sync);
void ui_core_runtime_bridge_sync_active_track_mirror(void);
void ui_core_runtime_bridge_sync_active_track_midi_channel(void);
void ui_core_runtime_bridge_sync_active_track_midi_source(void);
void ui_core_runtime_bridge_post_track_structure_change(uint8_t sync_active_track_ui_context);

#endif /* UI_CORE_RUNTIME_BRIDGE_H */
