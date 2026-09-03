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

#ifdef __cplusplus
extern "C" {
#endif

void control_domain_init(void);
void control_domain_start(float postgain, float output_compensation);
uint8_t control_domain_request_project(const control_project_intent_t *intent);
uint8_t control_domain_take_project(control_project_intent_t *intent);
uint8_t control_domain_request_patch(const control_patch_intent_t *intent);
uint8_t control_domain_take_patch(control_patch_intent_t *intent);
uint8_t control_domain_request_track(const control_track_intent_t *intent);
uint8_t control_domain_take_track(control_track_intent_t *intent);
uint8_t control_domain_request_routing(const control_routing_intent_t *intent);
uint8_t control_domain_take_routing(control_routing_intent_t *intent);
uint8_t control_domain_request_param(const control_param_intent_t *intent);
uint8_t control_domain_take_param(control_param_intent_t *intent);
uint8_t control_domain_request_seq(const control_seq_intent_t *intent);
uint8_t control_domain_take_seq(control_seq_intent_t *intent);
uint8_t control_domain_request_mod(const control_mod_intent_t *intent);
uint8_t control_domain_take_mod(control_mod_intent_t *intent);
uint8_t control_domain_request_macro(const control_macro_intent_t *intent);
uint8_t control_domain_take_macro(control_macro_intent_t *intent);
uint8_t control_domain_request_asset(const control_asset_intent_t *intent);
uint8_t control_domain_take_asset(control_asset_intent_t *intent);
uint8_t control_domain_request_clipboard(const control_clipboard_intent_t *intent);
uint8_t control_domain_take_clipboard(control_clipboard_intent_t *intent);
void control_domain_process_track_intents(void);
void control_domain_process_routing_intents(void);
void control_domain_process_param_intents(void);
void control_domain_process_seq_intents(void);
void control_domain_process_mod_intents(void);
void control_domain_process_macro_intents(void);
void control_domain_process_asset_intents(void);

#ifdef __cplusplus
}
#endif
