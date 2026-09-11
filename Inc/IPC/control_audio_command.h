#ifndef CONTROL_AUDIO_COMMAND_H
#define CONTROL_AUDIO_COMMAND_H

#include <stdint.h>

/* Shared M4 -> M7 functional ABI.  The seven opcodes are deliberately the
 * complete public grammar; sub-kinds only refine an opcode. */
typedef enum
{
    CONTROL_AUDIO_COMMAND_PROGRAM = 0U,
    CONTROL_AUDIO_COMMAND_PARAM,
    CONTROL_AUDIO_COMMAND_NOTE,
    CONTROL_AUDIO_COMMAND_TRANSPORT,
    CONTROL_AUDIO_COMMAND_RECORD,
    CONTROL_AUDIO_COMMAND_PANIC,
    CONTROL_AUDIO_COMMAND_AUDIO_STATE_COMMIT
} control_audio_command_opcode_t;

typedef enum { CONTROL_AUDIO_NOTE_OFF = 0U, CONTROL_AUDIO_NOTE_ON } control_audio_note_kind_t;
typedef enum
{
    CONTROL_AUDIO_TRANSPORT_START = 0U,
    CONTROL_AUDIO_TRANSPORT_STOP,
    CONTROL_AUDIO_TRANSPORT_CONTINUE,
    CONTROL_AUDIO_TRANSPORT_LOCATE
} control_audio_transport_kind_t;
typedef enum { CONTROL_AUDIO_RECORD_STOP = 0U, CONTROL_AUDIO_RECORD_START } control_audio_record_kind_t;
typedef enum { CONTROL_AUDIO_PANIC_GLOBAL = 0U, CONTROL_AUDIO_PANIC_ENTITY } control_audio_panic_kind_t;
typedef enum
{
    CONTROL_AUDIO_PARAM_BASE = 0U,
    CONTROL_AUDIO_PARAM_TEMP,
    CONTROL_AUDIO_PARAM_CLEAR_TEMP
} control_audio_param_semantic_t;

/* PARAM kinds encode both the target and its explicit base/override semantic. */
#define CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL       0U
#define CONTROL_AUDIO_PARAM_KIND_BASE_TRACK        1U
#define CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_FIRST 2U
#define CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_LAST  9U
#define CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK         10U
#define CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK   11U
typedef enum
{
    CONTROL_AUDIO_STATE_PATTERN = 0U,
    CONTROL_AUDIO_STATE_PROJECT
} control_audio_state_transition_kind_t;

typedef enum
{
    CONTROL_AUDIO_COMMAND_DURABLE_STATE = 0U,
    CONTROL_AUDIO_COMMAND_TRANSIENT_ACTION,
    CONTROL_AUDIO_COMMAND_RESOURCE_LIFECYCLE,
    CONTROL_AUDIO_COMMAND_REQUEST
} control_audio_command_state_class_t;

/* Reserved NOTE output identity for the one-shot monitor click.  It bypasses
 * the musical output ledger and is consumed before NOTE engine dispatch. */
#define CONTROL_AUDIO_NOTE_METRONOME_PREFIX 0xFFFFFF00UL
#define CONTROL_AUDIO_NOTE_METRONOME_MASK   0xFFFFFF00UL

#define CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER   (1U << 0)
#define CONTROL_AUDIO_PROGRAM_FLAG_CAN_SYNTH    (1U << 1)
#define CONTROL_AUDIO_PROGRAM_FLAG_CAN_PLAY     (1U << 2)
#define CONTROL_AUDIO_PROGRAM_VOICE_SHIFT       3U
#define CONTROL_AUDIO_PROGRAM_VOICE_MASK        (7U << CONTROL_AUDIO_PROGRAM_VOICE_SHIFT)
#define CONTROL_AUDIO_PROGRAM_ENCODE_VOICES(count) \
    ((uint8_t)((((count) - 1U) & 7U) << CONTROL_AUDIO_PROGRAM_VOICE_SHIFT))
#define CONTROL_AUDIO_PROGRAM_DECODE_VOICES(flags) \
    ((uint8_t)((((flags) & CONTROL_AUDIO_PROGRAM_VOICE_MASK) \
        >> CONTROL_AUDIO_PROGRAM_VOICE_SHIFT) + 1U))
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER (1U << 6)
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD  (1U << 7)
#define CONTROL_AUDIO_PROGRAM_FLAG_MASK          \
    (CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER       \
     | CONTROL_AUDIO_PROGRAM_FLAG_CAN_SYNTH      \
     | CONTROL_AUDIO_PROGRAM_FLAG_CAN_PLAY       \
     | CONTROL_AUDIO_PROGRAM_VOICE_MASK           \
     | CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER   \
     | CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD)

typedef struct
{
    uint8_t engine;
    uint8_t family;
    uint8_t type;
    uint8_t flags;
} control_audio_program_descriptor_t;

_Static_assert(sizeof(control_audio_program_descriptor_t) == sizeof(uint32_t),
               "PROGRAM payload must fit directly in command.value");

static inline uint32_t control_audio_program_pack(
    const control_audio_program_descriptor_t *descriptor)
{
    return (uint32_t)descriptor->engine
        | ((uint32_t)descriptor->family << 8)
        | ((uint32_t)descriptor->type << 16)
        | ((uint32_t)descriptor->flags << 24);
}

static inline control_audio_program_descriptor_t control_audio_program_unpack(
    uint32_t value)
{
    const control_audio_program_descriptor_t descriptor = {
        .engine = (uint8_t)value,
        .family = (uint8_t)(value >> 8),
        .type = (uint8_t)(value >> 16),
        .flags = (uint8_t)(value >> 24)
    };
    return descriptor;
}

/* Pure wire-format assertion.  The limits are ABI catalog limits supplied by
 * each endpoint; no mutable CONTROL or AUDIO state participates. */
static inline uint8_t control_audio_program_descriptor_is_structural(
    const control_audio_program_descriptor_t *descriptor,
    uint8_t engine_count, uint8_t family_max, uint8_t type_count)
{
    return (uint8_t)((descriptor != 0)
        && (descriptor->engine < engine_count)
        && (descriptor->family <= family_max)
        && (descriptor->type < type_count)
        && ((descriptor->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) == 0U
            || (descriptor->flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) == 0U));
}

typedef struct
{
    uint64_t effective_sample_time;
    uint32_t value;
    uint16_t id;
    uint8_t entity;
    uint8_t opcode_kind;
} control_audio_command_t;

#define CONTROL_AUDIO_COMMAND_OPCODE_MASK 0x07U
#define CONTROL_AUDIO_COMMAND_KIND_SHIFT 3U
#define CONTROL_AUDIO_COMMAND_TAG(opcode, kind) ((uint8_t)((opcode) | ((kind) << CONTROL_AUDIO_COMMAND_KIND_SHIFT)))
#define CONTROL_AUDIO_COMMAND_OPCODE(command) ((uint8_t)((command)->opcode_kind & CONTROL_AUDIO_COMMAND_OPCODE_MASK))
#define CONTROL_AUDIO_COMMAND_KIND(command) ((uint8_t)((command)->opcode_kind >> CONTROL_AUDIO_COMMAND_KIND_SHIFT))

_Static_assert(sizeof(control_audio_command_t) == 16U,
               "M4/M7 command ABI must remain 16 bytes");

/* Internal PARAM sub-ids still travel through the canonical command FIFO. */
#define CONTROL_AUDIO_FM_BASE_WORD_FIRST    0xFF80U
#define CONTROL_AUDIO_PARAM_PREVIEW_GAIN       0xFFC0U
#define CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE     0xFFC1U
#define CONTROL_AUDIO_PARAM_REC_BUS            0xFFC2U
#define CONTROL_AUDIO_PARAM_INPUT_OWNER        0xFFC4U
#define CONTROL_AUDIO_PARAM_LOOPER_ROUTE       0xFFC8U
#define CONTROL_AUDIO_PARAM_WAVETABLE_GEN      0xFFC9U
#define CONTROL_AUDIO_PARAM_WAVETABLE_SET      0xFFCAU
#define CONTROL_AUDIO_PARAM_MIDI_CONFIG         0xFFCBU
#define CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST 0xFFCCU
#define CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST 0xFFCDU
#define CONTROL_AUDIO_CONFIG_POLY_VOICES            0xFFD0U
#define CONTROL_AUDIO_MOD_ROUTE_SOURCE              0xFFD1U
#define CONTROL_AUDIO_MOD_ROUTE_DESTINATION         0xFFD2U
#define CONTROL_AUDIO_MOD_ROUTE_DEPTH               0xFFD3U
#define CONTROL_AUDIO_MOD_ROUTE_ENABLED             0xFFD4U
#define CONTROL_AUDIO_MOD_MULTI_SOURCE              0xFFD5U
#define CONTROL_AUDIO_MOD_SLEW_SOURCE               0xFFD6U
#define CONTROL_AUDIO_MOD_SLEW_AMOUNT               0xFFD7U
#define CONTROL_AUDIO_FX_FILTER_POSITION             0xFFD8U
#define CONTROL_AUDIO_FX_ORDER                       0xFFD9U
#define CONTROL_AUDIO_FX_SPATIAL_MODE                0xFFDAU
#define CONTROL_AUDIO_SAMPLER_ASSET                  0xFFDFU
#define CONTROL_AUDIO_LOOPER_PLAY_AUTO               0xFFCFU
#define CONTROL_AUDIO_PARAM_TRANSPORT_TEMPO        0xFFDCU
#define CONTROL_AUDIO_PARAM_TRANSPORT_STEP_Q16     0xFFDDU
#define CONTROL_AUDIO_PARAM_METRONOME_LEVEL        0xFFDEU
#define CONTROL_AUDIO_PARAM_MIX_ROUTE           0xFFE0U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST    0xFFE1U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_LAST     0xFFE4U
#define CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP 0xFFF5U
#define CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP   0xFFF6U
#define CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP  0xFFF7U

_Static_assert(CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP + 1U
                   == CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP,
               "resource-stop ABI ids must remain consecutive");
_Static_assert(CONTROL_AUDIO_PARAM_RAM_RESOURCE_STOP + 1U
                   == CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP,
               "resource-stop ABI ids must remain consecutive");

/* Durable/transient is a property of the wire command, not of PARAM identity.
 * This structural classification carries no CONTROL or AUDIO runtime policy. */
static inline control_audio_command_state_class_t
control_audio_command_state_class(const control_audio_command_t *command)
{
    if (command == 0) return CONTROL_AUDIO_COMMAND_TRANSIENT_ACTION;
    const uint8_t opcode = CONTROL_AUDIO_COMMAND_OPCODE(command);
    if (opcode == CONTROL_AUDIO_COMMAND_PROGRAM)
        return CONTROL_AUDIO_COMMAND_DURABLE_STATE;
    if (opcode != CONTROL_AUDIO_COMMAND_PARAM)
        return CONTROL_AUDIO_COMMAND_TRANSIENT_ACTION;
    if ((command->id == CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST)
            || (command->id == CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST))
        return CONTROL_AUDIO_COMMAND_REQUEST;
    if ((command->id >= CONTROL_AUDIO_PARAM_MULTI_RESOURCE_STOP)
            && (command->id <= CONTROL_AUDIO_PARAM_WAVE_RESOURCE_STOP))
        return CONTROL_AUDIO_COMMAND_RESOURCE_LIFECYCLE;
    if ((CONTROL_AUDIO_COMMAND_KIND(command)
                == CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK)
            || (CONTROL_AUDIO_COMMAND_KIND(command)
                == CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK))
        return CONTROL_AUDIO_COMMAND_TRANSIENT_ACTION;
    return CONTROL_AUDIO_COMMAND_DURABLE_STATE;
}

#endif
