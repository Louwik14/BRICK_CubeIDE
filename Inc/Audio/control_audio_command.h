#ifndef CONTROL_AUDIO_COMMAND_H
#define CONTROL_AUDIO_COMMAND_H

#include <stdint.h>

#include "Core/entity_topology.h"

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

typedef struct
{
    uint64_t effective_sample_time;
    uint32_t value;
    uint16_t id;
    brick_entity_id_t entity;
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
#define CONTROL_AUDIO_PARAM_TRANSITION_GLOBAL 0xFFDAU
#define CONTROL_AUDIO_PARAM_TRANSITION_TRACK  0xFFDBU
#define CONTROL_AUDIO_PARAM_PREVIEW_GAIN       0xFFC0U
#define CONTROL_AUDIO_PARAM_PREVIEW_ACTIVE     0xFFC1U
#define CONTROL_AUDIO_PARAM_REC_BUS            0xFFC2U
#define CONTROL_AUDIO_PARAM_INPUT_OWNER        0xFFC4U
#define CONTROL_AUDIO_PARAM_LOOPER_ROUTE       0xFFC8U
#define CONTROL_AUDIO_PARAM_WAVETABLE_GEN      0xFFC9U
#define CONTROL_AUDIO_PARAM_WAVETABLE_SET      0xFFCAU
#define CONTROL_AUDIO_PARAM_MIDI_CONFIG         0xFFCBU
#define CONTROL_AUDIO_PARAM_MIX_ROUTE           0xFFE0U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_FIRST    0xFFE1U
#define CONTROL_AUDIO_PARAM_MIX_INSERT_LAST     0xFFE4U

#endif
