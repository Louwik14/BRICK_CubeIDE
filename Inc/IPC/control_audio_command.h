#ifndef CONTROL_AUDIO_COMMAND_H
#define CONTROL_AUDIO_COMMAND_H

#include <stdint.h>

/* Shared M4 -> M7 functional ABI.  The six opcodes are deliberately the
 * complete public grammar; sub-kinds only refine an opcode. */
typedef enum
{
    CONTROL_AUDIO_COMMAND_PROGRAM = 0U,
    CONTROL_AUDIO_COMMAND_PARAM,
    CONTROL_AUDIO_COMMAND_NOTE,
    CONTROL_AUDIO_COMMAND_TRANSPORT,
    CONTROL_AUDIO_COMMAND_RECORD,
    CONTROL_AUDIO_COMMAND_PANIC
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

/* Reserved NOTE output identity for the one-shot monitor click.  It bypasses
 * the musical output ledger and is consumed before NOTE engine dispatch. */
#define CONTROL_AUDIO_NOTE_METRONOME_PREFIX 0xFFFFFF00UL
#define CONTROL_AUDIO_NOTE_METRONOME_MASK   0xFFFFFF00UL

#define CONTROL_AUDIO_PROGRAM_FLAG_CAN_FILTER   (1U << 0)
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER (1U << 6)
#define CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD  (1U << 7)

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
#define CONTROL_AUDIO_PARAM_TRANSPORT_TEMPO        0xFFDCU
#define CONTROL_AUDIO_PARAM_TRANSPORT_STEP_Q16     0xFFDDU
#define CONTROL_AUDIO_PARAM_MIX_ROUTE           0xFFE0U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST    0xFFE1U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_LAST     0xFFE4U

#endif
