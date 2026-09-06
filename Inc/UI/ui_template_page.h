#ifndef UI_TEMPLATE_PAGE_H
#define UI_TEMPLATE_PAGE_H

#include <stdint.h>

#include "Track/entity_topology.h"
#include "ui_core.h"
#include "ui_page.h"
#include "ui_param.h"
#include "ui_widgets.h"

typedef struct
{
    const char *title;
    ui_param_bank_t param_bank;
} ui_template_subpage_t;

typedef struct
{
    const char *family_title;
    const char *nav_labels[4];
    ui_template_subpage_t subpages[4];
    uint8_t default_subpage;
} ui_template_family_t;

typedef enum
{
    UI_TEMPLATE_FAMILY_ENV = 0,
    UI_TEMPLATE_FAMILY_CFG,
    UI_TEMPLATE_FAMILY_TONE,
    UI_TEMPLATE_FAMILY_MOD,
    UI_TEMPLATE_FAMILY_KEYBOARD,
    UI_TEMPLATE_FAMILY_FX,
    UI_TEMPLATE_FAMILY_SEQ,
    UI_TEMPLATE_FAMILY_MIX,
    UI_TEMPLATE_FAMILY_PLAY,
    UI_TEMPLATE_FAMILY_COUNT
} ui_template_family_id_t;

typedef struct
{
    uint8_t selected_entity;
    uint8_t owner_entity;
    entity_role_t role;
    ui_template_family_id_t family_id;
} ui_template_edit_context_t;

typedef enum
{
    UI_TEMPLATE_CUSTOM_WIDGET_NONE = 0,
    UI_TEMPLATE_CUSTOM_WIDGET_ADSR_FILTER,
    UI_TEMPLATE_CUSTOM_WIDGET_ADSR_VCA,
    UI_TEMPLATE_CUSTOM_WIDGET_ADSR_ENV3,
    UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE,
    UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CUTOFF,
    UI_TEMPLATE_CUSTOM_WIDGET_FILTER_RESONANCE,
    UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP,
    UI_TEMPLATE_CUSTOM_WIDGET_AUDIO_FX_GROUP,
    UI_TEMPLATE_CUSTOM_WIDGET_SPECTRAL_WINDOW_GROUP,
    UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK,
    UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE,
    UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE,
    UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL,
    UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_PHASE,
    UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP,
    UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SLOT,
    UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE,
    UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE,
    UI_TEMPLATE_CUSTOM_WIDGET_STACK_WAVEFORM,
    UI_TEMPLATE_CUSTOM_WIDGET_WAVE_WAVETABLE,
    UI_TEMPLATE_CUSTOM_WIDGET_FM_PITCH_EG_GROUP
} ui_template_custom_widget_kind_t;

typedef const ui_template_family_t *(*ui_template_family_resolver_fn)(void);

#define UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT 0xFFU
typedef uiw_widget_type_t (*ui_template_widget_picker_fn)(uint8_t slot,
                                                         param_id_t id,
                                                         const char *value_label,
                                                         uiw_widget_type_t suggested_widget);
typedef ui_template_custom_widget_kind_t (*ui_template_custom_widget_picker_fn)(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id);
typedef ui_template_custom_widget_kind_t (*ui_template_virtual_custom_widget_picker_fn)(uint8_t slot,
                                                                                          const ui_template_subpage_t *subpage);
typedef uint8_t (*ui_template_subpage_enabled_fn)(uint8_t subpage_index);
typedef uint8_t (*ui_template_virtual_slot_text_fn)(uint8_t slot,
                                                    char *out_name,
                                                    uint32_t out_name_len,
                                                    char *out_value,
                                                    uint32_t out_value_len);
typedef uint8_t (*ui_template_virtual_slot_value_fn)(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar);
typedef uint8_t (*ui_template_param_text_fn)(uint8_t slot,
                                             param_id_t id,
                                             float value,
                                             char *out_name,
                                             uint32_t out_name_len,
                                             char *out_value,
                                             uint32_t out_value_len);

typedef struct
{
    const ui_template_family_t *family;
    ui_template_family_resolver_fn family_resolver;
    ui_template_widget_picker_fn widget_picker;
    ui_template_custom_widget_picker_fn custom_widget_picker;
    ui_template_virtual_custom_widget_picker_fn virtual_custom_widget_picker;
    ui_template_subpage_enabled_fn subpage_enabled;
    ui_template_virtual_slot_text_fn virtual_slot_text;
    ui_template_virtual_slot_value_fn virtual_slot_value;
    ui_template_param_text_fn param_text;
    const ui_template_family_t *resolved_family;
    uint8_t active_subpage;
    uint8_t has_visited;
    uint8_t preserve_subpage_on_family_change;
    uint8_t navigation_subset;
    uint8_t resolved_navigation_subset;
} ui_template_page_state_t;

void ui_template_page_enter(void);
void ui_template_page_leave(void);
void ui_template_page_handle_event(const ui_event_t *ev);
void ui_template_page_sync_active_track_context(void);
void ui_template_page_render(void);

void ui_template_page_select_subpage(ui_template_page_state_t *state, uint8_t subpage_index);
void ui_template_page_select_nearest_subpage(ui_template_page_state_t *state, uint8_t subpage_index);
uint8_t ui_template_page_is_subpage_selectable(const ui_template_page_state_t *state, uint8_t subpage_index);
void ui_template_page_normalize_active_subpage(ui_template_page_state_t *state);
const ui_template_subpage_t *ui_template_page_get_active_subpage(const ui_template_page_state_t *state);
const ui_template_family_t *ui_template_page_get_active_family(const ui_template_page_state_t *state);

void ui_template_family_registry_init(void);
void ui_template_family_register(ui_template_family_id_t family_id,
                                 track_family_t track_family,
                                 track_type_t track_type,
                                 const ui_template_family_t *family);
const ui_template_family_t *ui_template_family_resolve(ui_template_family_id_t family_id,
                                                       uint8_t track,
                                                       track_family_t track_family,
                                                       track_type_t track_type);
const ui_template_family_t *ui_template_family_resolve_active_track(ui_template_family_id_t family_id);
uint8_t ui_template_edit_context_resolve(ui_template_family_id_t family_id,
                                         uint8_t selected_entity,
                                         ui_template_edit_context_t *out_context);
uint8_t ui_template_edit_context_resolve_active(ui_template_edit_context_t *out_context);
uint8_t ui_template_family_resolve_owner_track(ui_template_family_id_t family_id,
                                                uint8_t selected_track,
                                                uint8_t *out_owner_track);
const ui_template_family_t *ui_template_family_resolve_effective_for_track(ui_template_family_id_t family_id,
                                                                            uint8_t track,
                                                                            uint8_t scope_index);
const ui_template_family_t *ui_template_family_resolve_effective_active_track(ui_template_family_id_t family_id);
uint8_t ui_template_family_get_effective_scope_count(ui_template_family_id_t family_id, uint8_t track);

#endif /* UI_TEMPLATE_PAGE_H */
