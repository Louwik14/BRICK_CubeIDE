#ifndef PERSISTENT_CONTROL_MODEL_H
#define PERSISTENT_CONTROL_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical CONTROL persistence model.
 *
 * These are logical transfer objects, not an on-disk ABI and not runtime
 * snapshots.  A serializer must encode every field explicitly.  In
 * particular, it must never write one of these objects with sizeof().
 */

#define PERSIST_CONTROL_ENTITY_COUNT             16U
#define PERSIST_CONTROL_TOP_LEVEL_COUNT           8U
#define PERSIST_CONTROL_GROUP_MASTER_ID           7U
#define PERSIST_CONTROL_GROUP_CHILD_COUNT         8U
#define PERSIST_CONTROL_FIRST_GROUP_CHILD_ID       8U
#define PERSIST_CONTROL_STEP_COUNT                64U
#define PERSIST_CONTROL_PLAY_ITEM_COUNT            8U
#define PERSIST_CONTROL_CHILD_PLAY_ITEM_COUNT      1U
#define PERSIST_CONTROL_STEP_LOCK_COUNT            32U
#define PERSIST_CONTROL_ENTITY_PARAM_COUNT        320U
#define PERSIST_CONTROL_GLOBAL_PARAM_COUNT        128U
#define PERSIST_CONTROL_NOTE_FX_COUNT               3U
#define PERSIST_CONTROL_NOTE_FX_VALUE_COUNT         4U
#define PERSIST_CONTROL_MOD_LFO_COUNT               3U
#define PERSIST_CONTROL_MOD_ROUTE_COUNT             8U
#define PERSIST_CONTROL_PATTERN_BANK_COUNT         16U
#define PERSIST_CONTROL_PATTERN_PER_BANK           16U
#define PERSIST_CONTROL_MACRO_COUNT                 4U
#define PERSIST_CONTROL_MACRO_SCENE_COUNT          16U
#define PERSIST_CONTROL_MACRO_LOCK_COUNT           32U
#define PERSIST_CONTROL_ASSET_COUNT              1024U
#define PERSIST_CONTROL_ASSET_PATH_BYTES          160U
#define PERSIST_CONTROL_PATCH_NAME_BYTES           32U

typedef uint8_t persist_control_entity_id_t;
typedef uint32_t persist_control_parameter_key_t;
typedef uint32_t persist_control_family_key_t;
typedef uint32_t persist_control_type_key_t;
typedef uint32_t persist_control_asset_id_t;
typedef uint32_t persist_control_asset_kind_key_t;

#define PERSIST_CONTROL_KEY_NONE 0U
#define PERSIST_CONTROL_ASSET_NONE 0U

typedef enum
{
    PERSIST_ENTITY_ROLE_MAIN = 1,
    PERSIST_ENTITY_ROLE_GROUP_MASTER,
    PERSIST_ENTITY_ROLE_GROUP_CHILD
} persist_control_entity_role_t;

static inline persist_control_entity_role_t persist_control_entity_role(
    uint8_t group_active, persist_control_entity_id_t entity)
{
    if ((group_active != 0U) && (entity == PERSIST_CONTROL_GROUP_MASTER_ID))
    {
        return PERSIST_ENTITY_ROLE_GROUP_MASTER;
    }
    return ((group_active != 0U) && (entity >= PERSIST_CONTROL_FIRST_GROUP_CHILD_ID))
        ? PERSIST_ENTITY_ROLE_GROUP_CHILD : PERSIST_ENTITY_ROLE_MAIN;
}

static inline uint8_t persist_control_entity_play_limit(
    uint8_t group_active, persist_control_entity_id_t entity)
{
    return (persist_control_entity_role(group_active, entity) == PERSIST_ENTITY_ROLE_GROUP_CHILD)
        ? PERSIST_CONTROL_CHILD_PLAY_ITEM_COUNT : PERSIST_CONTROL_PLAY_ITEM_COUNT;
}

static inline uint8_t persist_control_entity_allows_note_fx(
    uint8_t group_active, persist_control_entity_id_t entity)
{
    return (persist_control_entity_role(group_active, entity) != PERSIST_ENTITY_ROLE_GROUP_MASTER)
        ? 1U : 0U;
}

static inline uint8_t persist_control_entity_is_mod_owner(
    uint8_t group_active, persist_control_entity_id_t entity)
{
    if (entity >= PERSIST_CONTROL_ENTITY_COUNT) return 0U;
    if ((group_active != 0U) && (entity >= PERSIST_CONTROL_FIRST_GROUP_CHILD_ID)) return 0U;
    return 1U;
}

/* Stable product keys. Their numeric values are explicit contracts and must
 * not be replaced by casts from internal C enums. */
typedef enum
{
    PERSIST_FAMILY_OFF      = 0x4F464620UL, /* OFF  */
    PERSIST_FAMILY_SYNTH    = 0x53594E54UL, /* SYNT */
    PERSIST_FAMILY_DRUM     = 0x4452554DUL, /* DRUM */
    PERSIST_FAMILY_MIDI     = 0x4D494449UL, /* MIDI */
    PERSIST_FAMILY_SAMPLER  = 0x53414D50UL, /* SAMP */
    PERSIST_FAMILY_EXTERNAL = 0x45585420UL  /* EXT  */
} persist_control_family_key_value_t;

typedef enum
{
    PERSIST_TYPE_NONE           = 0x4E4F4E45UL, /* NONE */
    PERSIST_TYPE_PRISM          = 0x50524953UL, /* PRIS */
    PERSIST_TYPE_WAVE           = 0x57415645UL, /* WAVE */
    PERSIST_TYPE_STACK          = 0x5354414BUL, /* STAK */
    PERSIST_TYPE_FM             = 0x464D2020UL, /* FM   */
    PERSIST_TYPE_DRUM_MD        = 0x4D444D20UL, /* MDM  */
    PERSIST_TYPE_DRUM_ANALOG_BD = 0x414E4244UL, /* ANBD */
    PERSIST_TYPE_MIDI           = 0x4D494449UL, /* MIDI */
    PERSIST_TYPE_RAM_SAMPLE     = 0x52414D20UL, /* RAM  */
    PERSIST_TYPE_STREAM_SAMPLE  = 0x5354524DUL, /* STRM */
    PERSIST_TYPE_MULTI_SAMPLE   = 0x4D554C54UL, /* MULT */
    PERSIST_TYPE_LOOPER         = 0x4C4F4F50UL, /* LOOP */
    PERSIST_TYPE_EXTERNAL       = 0x45585420UL, /* EXT  */
    PERSIST_TYPE_GROUP          = 0x47525020UL  /* GRP  */
} persist_control_type_key_value_t;

typedef enum
{
    PERSIST_ASSET_SAMPLE_STREAM = 0x5354524DUL, /* STRM */
    PERSIST_ASSET_SAMPLE_RAM    = 0x52414D20UL, /* RAM  */
    PERSIST_ASSET_MULTI         = 0x4D554C54UL, /* MULT */
    PERSIST_ASSET_WAVETABLE     = 0x57415645UL  /* WAVE */
} persist_control_asset_kind_key_value_t;

typedef enum
{
    PERSIST_MACRO_HALL_SCENE  = 0x53434E45UL, /* SCNE */
    PERSIST_MACRO_HALL_SWITCH = 0x53574954UL  /* SWIT */
} persist_control_macro_hall_key_value_t;

typedef enum
{
    PERSIST_MIDI_SOURCE_INTERNAL = 0x494E5420UL, /* INT  */
    PERSIST_MIDI_SOURCE_EXTERNAL = 0x45585420UL, /* EXT  */
    PERSIST_MIDI_SOURCE_ALL      = 0x414C4C20UL  /* ALL  */
} persist_control_midi_source_key_value_t;

typedef enum
{
    PERSIST_CLOCK_INTERNAL = 0x494E5420UL, /* INT  */
    PERSIST_CLOCK_MIDI     = 0x4D494449UL, /* MIDI */
    PERSIST_CLOCK_USB      = 0x55534220UL  /* USB  */
} persist_control_clock_key_value_t;

typedef enum
{
    PERSIST_NOTE_FX_OFF    = 0x4F464620UL, /* OFF  */
    PERSIST_NOTE_FX_ARP    = 0x41525020UL, /* ARP  */
    PERSIST_NOTE_FX_EUCLID = 0x4555434CUL  /* EUCL */
} persist_control_note_fx_model_key_value_t;

typedef enum
{
    PERSIST_MOD_SOURCE_NONE    = 0x4E4F4E45UL, /* NONE */
    PERSIST_MOD_SOURCE_LFO1    = 0x4C464F31UL, /* LFO1 */
    PERSIST_MOD_SOURCE_LFO2    = 0x4C464F32UL, /* LFO2 */
    PERSIST_MOD_SOURCE_LFO3    = 0x4C464F33UL, /* LFO3 */
    PERSIST_MOD_SOURCE_ENV_FLT = 0x454E464CUL, /* ENFL */
    PERSIST_MOD_SOURCE_ENV_VCA = 0x454E5643UL, /* ENVC */
    PERSIST_MOD_SOURCE_ENV_MOD = 0x454E4D44UL, /* ENMD */
    PERSIST_MOD_SOURCE_MULTI1  = 0x4D554C31UL, /* MUL1 */
    PERSIST_MOD_SOURCE_MULTI2  = 0x4D554C32UL, /* MUL2 */
    PERSIST_MOD_SOURCE_SLEW1   = 0x534C5731UL, /* SLW1 */
    PERSIST_MOD_SOURCE_SLEW2   = 0x534C5732UL  /* SLW2 */
} persist_control_mod_source_key_value_t;

typedef enum { PERSIST_INPUT_NONE=0x4E4F4E45UL, PERSIST_INPUT_PHYSICAL_1=0x494E5031UL } persist_control_input_key_value_t;
typedef enum { PERSIST_LFO_SHAPE_SINE=0x53494E45UL,PERSIST_LFO_SHAPE_TRIANGLE=0x54524920UL,PERSIST_LFO_SHAPE_SAW=0x53415720UL,PERSIST_LFO_SHAPE_SQUARE=0x53515220UL,PERSIST_LFO_SHAPE_RANDOM_SH=0x524E444DUL,PERSIST_LFO_SHAPE_SINE_POS=0x53494E50UL,PERSIST_LFO_SHAPE_TRIANGLE_POS=0x54524950UL,PERSIST_LFO_SHAPE_SQUARE_POS=0x53515250UL,PERSIST_LFO_SHAPE_REVERSE_SAW=0x52534157UL } persist_control_lfo_shape_key_value_t;
typedef enum { PERSIST_LFO_TRIGGER_FREE=0x46524545UL,PERSIST_LFO_TRIGGER_TRIG=0x54524947UL,PERSIST_LFO_TRIGGER_HOLD=0x484F4C44UL,PERSIST_LFO_TRIGGER_ONE=0x4F4E4520UL,PERSIST_LFO_TRIGGER_POLY_TRIG=0x50545247UL,PERSIST_LFO_TRIGGER_POLY_HOLD=0x50484C44UL,PERSIST_LFO_TRIGGER_POLY_ONE=0x504F4E45UL } persist_control_lfo_trigger_key_value_t;
typedef enum { PERSIST_RECORD_START_DEFAULT=0x44454620UL,PERSIST_RECORD_START_TRIGGER=0x54524947UL,PERSIST_RECORD_START_ROLL_QUARTER=0x52313420UL,PERSIST_RECORD_START_ROLL_HALF=0x52313220UL,PERSIST_RECORD_START_ROLL_BAR=0x52313120UL } persist_control_record_start_key_value_t;
typedef enum { PERSIST_RECORD_LENGTH_OVERDUB=0x4F564552UL,PERSIST_RECORD_LENGTH_PATTERN=0x50415454UL } persist_control_record_length_key_value_t;

/* Parameter keys use an explicit namespace plus a stable semantic code.  The
 * parameter catalog owns the eventual internal-ID <-> key translation. */
typedef enum
{
    PERSIST_PARAM_NAMESPACE_CONFIG = 0x01U,
    PERSIST_PARAM_NAMESPACE_TONE   = 0x02U,
    PERSIST_PARAM_NAMESPACE_ENV    = 0x03U,
    PERSIST_PARAM_NAMESPACE_MIX    = 0x04U,
    PERSIST_PARAM_NAMESPACE_MOD    = 0x05U,
    PERSIST_PARAM_NAMESPACE_PLAY   = 0x06U,
    PERSIST_PARAM_NAMESPACE_GLOBAL = 0x07U
} persist_control_parameter_namespace_t;

#define PERSIST_CONTROL_PARAMETER_KEY(_namespace, _semantic_code) \
    ((((uint32_t)(_namespace)) << 24U) | ((uint32_t)(_semantic_code) & 0x00FFFFFFUL))

typedef enum
{
    PERSIST_VALUE_BOOL = 1,
    PERSIST_VALUE_U8,
    PERSIST_VALUE_U16,
    PERSIST_VALUE_I16,
    PERSIST_VALUE_U32,
    PERSIST_VALUE_I32,
    PERSIST_VALUE_FLOAT32
} persist_control_value_kind_t;

typedef union
{
    uint8_t boolean;
    uint8_t u8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    float f32;
} persist_control_value_t;

typedef struct
{
    persist_control_parameter_key_t key;
    persist_control_value_kind_t kind;
    persist_control_value_t value;
} persist_control_parameter_t;

typedef struct
{
    persist_control_asset_id_t id;
    persist_control_asset_kind_key_t kind;
    uint16_t path_length;
    char path[PERSIST_CONTROL_ASSET_PATH_BYTES];
} persist_control_asset_ref_t;

typedef struct
{
    uint8_t note;
    uint8_t velocity;
    uint8_t length;
    int8_t microtiming;
    uint8_t present_mask;
} persist_control_play_item_t;

typedef struct
{
    persist_control_parameter_key_t parameter;
    uint8_t flags;
    persist_control_value_kind_t kind;
    persist_control_value_t value;
} persist_control_step_lock_t;

typedef struct
{
    uint8_t trigger;
    uint8_t roll;
    uint8_t play_count;
    uint8_t lock_count;
    persist_control_play_item_t play[PERSIST_CONTROL_PLAY_ITEM_COUNT];
    persist_control_step_lock_t locks[PERSIST_CONTROL_STEP_LOCK_COUNT];
} persist_control_step_t;

typedef struct
{
    uint8_t length;
    uint8_t division;
    uint8_t quantization;
    uint8_t swing;
    persist_control_step_t steps[PERSIST_CONTROL_STEP_COUNT];
} persist_control_sequence_t;

typedef struct
{
    uint32_t model_key;
    uint8_t values[PERSIST_CONTROL_NOTE_FX_VALUE_COUNT];
} persist_control_note_fx_t;

typedef struct
{
    float rate;
    uint32_t shape_key;
    uint32_t trigger_key;
    float phase_offset;
} persist_control_mod_lfo_t;

typedef struct
{
    uint32_t source_a_key;
    uint32_t source_b_key;
} persist_control_mod_multi_t;

typedef struct
{
    uint32_t source_key;
    float amount;
} persist_control_mod_slew_t;

typedef struct
{
    float attack;
    float decay;
    float sustain;
    float release;
    uint8_t retrigger_hard;
} persist_control_mod_envelope_t;

typedef struct
{
    uint32_t source_key;
    persist_control_entity_id_t destination_entity;
    persist_control_parameter_key_t destination_parameter;
    float depth;
    uint8_t enabled;
} persist_control_mod_route_t;

typedef struct
{
    persist_control_mod_lfo_t lfos[PERSIST_CONTROL_MOD_LFO_COUNT];
    persist_control_mod_multi_t multi[2U];
    persist_control_mod_slew_t slew[2U];
    persist_control_mod_envelope_t envelope;
    persist_control_mod_route_t routes[PERSIST_CONTROL_MOD_ROUTE_COUNT];
} persist_control_modulation_t;

typedef struct
{
    persist_control_entity_id_t entity_id;
    persist_control_family_key_t family;
    persist_control_type_key_t type;
    uint8_t midi_channel;
    uint32_t midi_source_key;
    uint32_t input_key;
    uint8_t muted;
    persist_control_asset_id_t asset;
    uint16_t parameter_count;
    persist_control_parameter_t parameters[PERSIST_CONTROL_ENTITY_PARAM_COUNT];
    uint8_t note_fx_count;
    persist_control_note_fx_t note_fx[PERSIST_CONTROL_NOTE_FX_COUNT];
    uint8_t modulation_present;
    persist_control_modulation_t modulation;
    persist_control_sequence_t sequence;
} persist_control_entity_t;

typedef enum
{
    PERSIST_ROUTE_LOOPER_SOURCE = 0x4C4F4F50UL /* LOOP */
} persist_control_route_kind_t;

typedef struct
{
    uint32_t kind;
    persist_control_entity_id_t source;
    persist_control_entity_id_t destination;
    uint8_t enabled;
} persist_control_route_t;

typedef struct
{
    uint32_t tempo_milli_bpm;
    uint32_t clock_source_key;
    uint32_t record_start_key;
    uint32_t record_length_key;
    uint16_t parameter_count;
    persist_control_parameter_t parameters[PERSIST_CONTROL_GLOBAL_PARAM_COUNT];
} persist_control_globals_t;

typedef struct
{
    persist_control_entity_id_t entity;
    persist_control_parameter_key_t parameter;
    persist_control_value_kind_t kind;
    persist_control_value_t value;
} persist_control_macro_lock_t;

typedef struct
{
    uint8_t lock_count;
    persist_control_macro_lock_t locks[PERSIST_CONTROL_MACRO_LOCK_COUNT];
} persist_control_macro_scene_t;

typedef struct
{
    uint32_t hall_switch_key;
    uint8_t selected_scene[PERSIST_CONTROL_MACRO_COUNT];
    persist_control_macro_scene_t scenes[PERSIST_CONTROL_MACRO_SCENE_COUNT];
} persist_control_macros_t;

typedef struct
{
    persist_control_entity_t entities[PERSIST_CONTROL_ENTITY_COUNT];
    uint16_t route_count;
    persist_control_route_t routes[PERSIST_CONTROL_ENTITY_COUNT * PERSIST_CONTROL_ENTITY_COUNT];
    persist_control_globals_t globals;
} persist_control_pattern_t;

typedef struct
{
    uint8_t bank;
    uint8_t pattern;
    uint8_t present;
    persist_control_pattern_t content;
} persist_control_pattern_record_t;

typedef struct
{
    uint16_t name_length;
    char name[PERSIST_CONTROL_PATCH_NAME_BYTES];
    persist_control_family_key_t family;
    persist_control_type_key_t type;
    persist_control_asset_ref_t asset;
    uint16_t parameter_count;
    persist_control_parameter_t parameters[PERSIST_CONTROL_ENTITY_PARAM_COUNT];
} persist_control_patch_t;

_Static_assert(PERSIST_CONTROL_ENTITY_COUNT
                   == (PERSIST_CONTROL_TOP_LEVEL_COUNT + PERSIST_CONTROL_GROUP_CHILD_COUNT),
               "canonical project must represent all sixteen entities");
_Static_assert(PERSIST_CONTROL_GROUP_MASTER_ID == 7U,
               "GROUP master persistent identity changed");
_Static_assert(PERSIST_CONTROL_FIRST_GROUP_CHILD_ID == 8U,
               "GROUP child persistent identity changed");
_Static_assert(PERSIST_CONTROL_PLAY_ITEM_COUNT == 8U,
               "top-level and GROUP-master PLAY capacity changed");
_Static_assert(PERSIST_CONTROL_CHILD_PLAY_ITEM_COUNT == 1U,
               "GROUP-child PLAY capacity changed");

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_CONTROL_MODEL_H */
