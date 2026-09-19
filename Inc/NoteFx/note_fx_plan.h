#ifndef NOTE_FX_PLAN_H
#define NOTE_FX_PLAN_H

#include <stdint.h>

#include "NoteFx/note_fx_state.h"

#define NOTE_FX_PLAN_FIRST_SLOT_NONE NOTE_FX_SLOT_COUNT

typedef enum
{
    NOTE_FX_PLAN_FLAG_MODIFIER = (1U << 0),
    NOTE_FX_PLAN_FLAG_FAN_OUT = (1U << 1),
    NOTE_FX_PLAN_FLAG_TEMPORAL = (1U << 2),
    NOTE_FX_PLAN_FLAG_HELD = (1U << 3)
} note_fx_plan_flag_t;

/* Compact directly executable slot word:
 * [3:0] opcode/model, [7:4] flags, [15:8] p1, [23:16] p2, [31:24] p3. */
typedef uint32_t note_fx_slot_plan_word_t;

typedef struct __attribute__((packed))
{
    note_fx_slot_plan_word_t slot[NOTE_FX_SLOT_COUNT];
    uint16_t override_mask;
    uint8_t active_mask;
    uint8_t first_active_slot;
} note_fx_compiled_plan_t;

_Static_assert(sizeof(note_fx_compiled_plan_t) == 20U,
               "compiled four-slot plan layout changed");

uint8_t note_fx_plan_compile(const note_fx_track_state_t *effective,
                             uint16_t override_mask,
                             note_fx_compiled_plan_t *out_plan);
uint8_t note_fx_plan_model(note_fx_slot_plan_word_t word);
uint8_t note_fx_plan_flags(note_fx_slot_plan_word_t word);
uint8_t note_fx_plan_param(note_fx_slot_plan_word_t word, uint8_t param);

#endif
