#pragma once

#include <stdint.h>
#include "Param/param_ids.h"

typedef enum
{
    CONTROL_PROJECT_SAVE = 0,
    CONTROL_PROJECT_LOAD,
    CONTROL_PROJECT_DELETE,
    CONTROL_PROJECT_BLANK,
    CONTROL_PROJECT_RESTORE_BOOT
} control_project_operation_t;

typedef struct
{
    control_project_operation_t operation;
    uint8_t slot;
} control_project_intent_t;

typedef enum
{
    CONTROL_PATCH_SAVE = 0,
    CONTROL_PATCH_APPLY,
    CONTROL_PATCH_RENAME,
    CONTROL_PATCH_DELETE
} control_patch_operation_t;

typedef struct
{
    control_patch_operation_t operation;
    uint16_t slot;
    uint16_t target_mask;
    uint8_t entity;
    char name[33];
} control_patch_intent_t;

typedef enum
{
    CONTROL_TRACK_SET_STRUCTURE = 0,
    CONTROL_TRACK_SET_TYPE,
    CONTROL_TRACK_SET_MIDI_CHANNEL,
    CONTROL_TRACK_SET_MIDI_SOURCE,
    CONTROL_TRACK_SET_EXTERNAL_INPUT,
    CONTROL_TRACK_SET_MUTE,
    CONTROL_TRACK_SET_MUTE_MASK,
    CONTROL_TRACK_SET_LOOPER_CONFIG
} control_track_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t track;
    uint8_t value0;
    uint8_t value1;
    uint8_t value2;
    uint16_t mute_mask;
} control_track_intent_t;

typedef struct
{
    uint8_t looper;
    uint8_t source;
    uint8_t enabled;
    uint8_t reserved;
} control_routing_intent_t;

typedef enum
{
    CONTROL_PARAM_SCOPE_GLOBAL = 0,
    CONTROL_PARAM_SCOPE_TRACK = 1
} control_param_scope_t;

typedef struct
{
    param_id_t parameter_id;
    uint8_t scope;
    uint8_t track;
    float value;
} control_param_intent_t;

typedef enum
{
    CONTROL_SEQ_STEP_PRESS = 0,
    CONTROL_SEQ_STEP_RELEASE,
    CONTROL_SEQ_CHANGE_PAGE,
    CONTROL_SEQ_ROLL_DELTA,
    CONTROL_SEQ_SET_LENGTH,
    CONTROL_SEQ_SET_DIVISION,
    CONTROL_SEQ_SET_QUANTIZATION,
    CONTROL_SEQ_SET_SWING,
    CONTROL_SEQ_PLAY_SET,
    CONTROL_SEQ_PLOCK_UPSERT,
    CONTROL_SEQ_PLOCK_DELETE,
    CONTROL_SEQ_LIVE_REC_PLOCK_WRITE,
    CONTROL_SEQ_TRANSPORT_TOGGLE,
    CONTROL_SEQ_RECORD_TARGET_ARM,
    CONTROL_SEQ_SET_RECORD_LENGTH_MODE,
    CONTROL_SEQ_SET_RECORD_START_MODE,
    CONTROL_SEQ_SET_TEMPO,
    CONTROL_SEQ_SET_CLOCK_SOURCE,
    CONTROL_SEQ_SET_METRONOME
} control_seq_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t track;
    uint8_t step;
    uint8_t voice;
    uint8_t field;
    uint8_t set_id;
    uint8_t param_slot;
    uint8_t flags;
    uint16_t value16;
    int16_t value;
    int8_t delta;
    uint8_t reserved;
    uint32_t value32;
} control_seq_intent_t;

typedef enum
{
    CONTROL_MOD_SET_SELECTED_SLOT = 0,
    CONTROL_MOD_SET_SELECTED_SOURCE,
    CONTROL_MOD_SET_SELECTED_DESTINATION,
    CONTROL_MOD_SET_SELECTED_DEPTH,
    CONTROL_MOD_SET_MULTI_SOURCE,
    CONTROL_MOD_SET_SLEW_SOURCE,
    CONTROL_MOD_SET_SLEW_AMOUNT
} control_mod_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t track;
    uint8_t index;
    uint8_t input;
    float value;
} control_mod_intent_t;

typedef enum
{
    CONTROL_MACRO_ASSIGN_SCENE_LOCK = 0,
    CONTROL_MACRO_CLEAR_SCENE_LOCK,
    CONTROL_MACRO_SET_SCENE_SOURCE_AMOUNT,
    CONTROL_MACRO_RELEASE_SCENE_SOURCE,
    CONTROL_MACRO_RELEASE_ALL_SCENE_SOURCES,
    CONTROL_MACRO_SET_HALL_MODE
} control_macro_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t scene;
    uint8_t track;
    uint8_t reserved;
    uint16_t lock;
    param_id_t parameter;
    float value;
} control_macro_intent_t;

typedef enum
{
    CONTROL_ASSET_SELECT_TRACK_LOGICAL = 0,
    CONTROL_ASSET_REGISTER_RUNTIME,
    CONTROL_ASSET_REMOVE_RUNTIME
} control_asset_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t track;
    uint8_t role;
    uint32_t kind;
    uint16_t logical;
    uint16_t runtime;
} control_asset_intent_t;

typedef enum
{
    CONTROL_CLIPBOARD_APPLY_MACRO_LOCK = 0,
    CONTROL_CLIPBOARD_CLEAR_MACRO_LOCK,
    CONTROL_CLIPBOARD_APPLY_TRACK,
    CONTROL_CLIPBOARD_CLEAR_TRACK,
    CONTROL_CLIPBOARD_APPLY_ENSEMBLE,
    CONTROL_CLIPBOARD_CLEAR_ENSEMBLE,
    CONTROL_CLIPBOARD_APPLY_PAGE,
    CONTROL_CLIPBOARD_CLEAR_PAGE,
    CONTROL_CLIPBOARD_APPLY_SEQUENCE,
    CONTROL_CLIPBOARD_CLEAR_SEQUENCE
} control_clipboard_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t target;
    uint8_t arg0;
    uint8_t arg1;
} control_clipboard_intent_t;

typedef enum
{
    CONTROL_KEYBOARD_SET_ROOT = 0U,
    CONTROL_KEYBOARD_SET_SCALE,
    CONTROL_KEYBOARD_SET_OMNICHORD,
    CONTROL_KEYBOARD_SET_NOTE_ORDER,
    CONTROL_KEYBOARD_SET_CHORD_OVERRIDE,
    CONTROL_KEYBOARD_SET_MONO_LAST,
    CONTROL_KEYBOARD_STEP_OCTAVE,
    CONTROL_KEYBOARD_SET_VELOCITY_PROFILE,
    CONTROL_KEYBOARD_SET_VELOCITY_MODE,
    CONTROL_KEYBOARD_SET_VELOCITY_CURVE
} control_keyboard_operation_t;

typedef struct
{
    uint8_t operation;
    int8_t value;
    uint8_t reserved[2];
} control_keyboard_intent_t;

typedef enum
{
    CONTROL_AUDIO_FX_SET_FILTER_POSITION = 0U,
    CONTROL_AUDIO_FX_SET_ORDER,
    CONTROL_AUDIO_FX_SET_SPATIAL_MODE
} control_audio_fx_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t entity;
    uint8_t slot;
    uint8_t value;
} control_audio_fx_intent_t;

typedef struct
{
    uint8_t track;
    uint8_t voices;
} control_polyphony_intent_t;

typedef enum
{
    CONTROL_AUDIO_REC_TOGGLE_ROUTE = 0U,
    CONTROL_AUDIO_REC_SET_ARM,
    CONTROL_AUDIO_REC_STEP_ARM,
    CONTROL_AUDIO_REC_STEP_LENGTH,
    CONTROL_AUDIO_REC_STEP_QUANTIZATION,
    CONTROL_AUDIO_REC_STEP_THRESHOLD,
    CONTROL_AUDIO_REC_TOGGLE_LINE,
    CONTROL_AUDIO_REC_TOGGLE_MIC,
    CONTROL_AUDIO_REC_TOGGLE_USB,
    CONTROL_AUDIO_REC_STEP_EDIT,
    CONTROL_AUDIO_REC_RETURN,
    CONTROL_AUDIO_REC_AUDITION,
    CONTROL_AUDIO_REC_SAVE,
    CONTROL_AUDIO_REC_ASSIGN,
    CONTROL_AUDIO_REC_TOGGLE_ZCROSS,
    CONTROL_AUDIO_REC_STOP_CLIENT,
    CONTROL_AUDIO_REC_START_AT
} control_audio_rec_operation_t;

typedef struct
{
    uint8_t operation;
    uint8_t value0;
    uint8_t value1;
    uint8_t value2;
    int16_t delta;
    uint8_t has_sample_time;
    uint8_t reserved;
    uint64_t sample_time;
} control_audio_rec_intent_t;

typedef struct
{
    uint8_t redo;
} control_history_intent_t;

typedef struct
{
    uint8_t operation;
    uint8_t entity;
    uint8_t slot;
    uint8_t value;
} control_audio_visual_intent_t;

typedef struct
{
    float gain;
} control_preview_gain_intent_t;

typedef struct
{
    uint16_t source_entity_mask;
    uint8_t arm;
    uint8_t source_flags;
    uint8_t has_sample_time;
    uint8_t reserved[3];
    uint64_t sample_time;
} control_rec_bus_intent_t;

typedef enum
{
    CONTROL_STORAGE_UI_CANCEL_MULTI_LOAD = 0U,
    CONTROL_STORAGE_UI_CLEAR_CONVERSION
} control_storage_ui_operation_t;

typedef struct
{
    uint8_t operation;
} control_storage_ui_intent_t;

typedef enum
{
    CONTROL_STORAGE_EVENT_AUDIO_PARAM = 0U,
    CONTROL_STORAGE_EVENT_RECORD_STOP
} control_storage_event_type_t;

/* Final Storage -> CONTROL event shape.  PASS 2 uses the audio command
 * variants; the identity fields are already part of the contract for the
 * lifecycle/result work that follows. */
typedef struct
{
    uint8_t type;
    uint8_t family;
    uint8_t requester;
    uint8_t result;
    uint32_t request_id;
    uint32_t epoch;
    uint32_t resource_id;
    uint8_t entity;
    uint8_t client;
    uint16_t parameter_id;
    uint32_t value;
} control_storage_audio_event_t;

typedef enum
{
    CONTROL_UI_MSG_PROJECT = 0U,
    CONTROL_UI_MSG_PATCH,
    CONTROL_UI_MSG_TRACK,
    CONTROL_UI_MSG_ROUTING,
    CONTROL_UI_MSG_PARAM,
    CONTROL_UI_MSG_SEQ,
    CONTROL_UI_MSG_MOD,
    CONTROL_UI_MSG_MACRO,
    CONTROL_UI_MSG_ASSET,
    CONTROL_UI_MSG_CLIPBOARD,
    CONTROL_UI_MSG_KEYBOARD,
    CONTROL_UI_MSG_AUDIO_FX,
    CONTROL_UI_MSG_POLYPHONY,
    CONTROL_UI_MSG_AUDIO_REC,
    CONTROL_UI_MSG_HISTORY,
    CONTROL_UI_MSG_AUDIO_VISUAL,
    CONTROL_UI_MSG_PREVIEW_GAIN,
    CONTROL_UI_MSG_REC_BUS,
    CONTROL_UI_MSG_STORAGE
} control_ui_message_type_t;

typedef union
{
    control_project_intent_t project;
    control_patch_intent_t patch;
    control_track_intent_t track;
    control_routing_intent_t routing;
    control_param_intent_t param;
    control_seq_intent_t seq;
    control_mod_intent_t mod;
    control_macro_intent_t macro;
    control_asset_intent_t asset;
    control_clipboard_intent_t clipboard;
    control_keyboard_intent_t keyboard;
    control_audio_fx_intent_t audio_fx;
    control_polyphony_intent_t polyphony;
    control_audio_rec_intent_t audio_rec;
    control_history_intent_t history;
    control_audio_visual_intent_t audio_visual;
    control_preview_gain_intent_t preview_gain;
    control_rec_bus_intent_t rec_bus;
    control_storage_ui_intent_t storage;
} control_ui_message_payload_t;

typedef struct
{
    uint8_t type;
    uint8_t reserved[3];
    control_ui_message_payload_t payload;
} control_ui_message_t;

#define CONTROL_UI_FIFO_CAPACITY 256U

_Static_assert(sizeof(control_ui_message_t) <= 64U,
               "UI to CONTROL message must remain bounded");

#ifdef __cplusplus
extern "C" {
#endif

void control_domain_init(void);
void control_domain_start(float postgain, float output_compensation);
uint8_t control_domain_request_project(const control_project_intent_t *intent);
uint8_t control_domain_request_patch(const control_patch_intent_t *intent);
uint8_t control_domain_request_track(const control_track_intent_t *intent);
uint8_t control_domain_request_routing(const control_routing_intent_t *intent);
uint8_t control_domain_request_param(const control_param_intent_t *intent);
uint8_t control_domain_request_seq(const control_seq_intent_t *intent);
uint8_t control_domain_request_mod(const control_mod_intent_t *intent);
uint8_t control_domain_request_macro(const control_macro_intent_t *intent);
uint8_t control_domain_request_asset(const control_asset_intent_t *intent);
uint8_t control_domain_request_clipboard(const control_clipboard_intent_t *intent);
uint8_t control_domain_request_keyboard(uint8_t operation, int8_t value);
uint8_t control_domain_request_audio_fx(const control_audio_fx_intent_t *intent);
uint8_t control_domain_request_polyphony(uint8_t track, uint8_t voices);
uint8_t control_domain_request_audio_rec(const control_audio_rec_intent_t *intent);
uint8_t control_domain_request_history(uint8_t redo);
uint8_t control_domain_request_audio_visual(const control_audio_visual_intent_t *intent);
uint8_t control_domain_request_preview_gain(float gain);
uint8_t control_domain_request_rec_bus(uint16_t source_entity_mask,
                                       uint8_t arm, uint8_t source_flags,
                                       uint8_t has_sample_time,
                                       uint64_t sample_time);
uint8_t control_domain_request_storage_ui(uint8_t operation);
uint8_t control_domain_request_storage_audio_param(uint8_t entity,
                                                   uint16_t parameter_id,
                                                   uint32_t value);
uint8_t control_domain_request_storage_record_stop(uint8_t client);
uint8_t control_domain_request_storage_waveform_cache(const char *path,
                                                      uint8_t reason,
                                                      uint32_t frame_count,
                                                      uint32_t sample_rate);
void control_domain_storage_process_requests(void);
uint32_t control_domain_ui_overflow_count(void);
uint32_t control_domain_ui_pending_count(void);
uint32_t control_domain_storage_pending_count(void);
uint8_t control_domain_project_ui_busy(void);
void control_domain_process_ui_messages(void);
void control_domain_process_storage_messages(void);

#ifdef __cplusplus
}
#endif
