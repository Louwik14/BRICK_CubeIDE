#ifndef NOTE_FX_PLAN_H
#define NOTE_FX_PLAN_H

#include <stdint.h>

#include "NoteFx/note_fx_state.h"

typedef struct __attribute__((packed))
{
    uint8_t model;
    uint8_t param[NOTE_FX_PARAM_COUNT];
} note_fx_slot_plan_word_t;

_Static_assert(sizeof(note_fx_slot_plan_word_t) == 5U,
               "four-parameter Note FX slot plan budget");

typedef struct __attribute__((packed))
{
    note_fx_slot_plan_word_t slot[NOTE_FX_SLOT_COUNT];
    uint8_t order;
} note_fx_compiled_plan_t;

_Static_assert(sizeof(note_fx_compiled_plan_t) == 16U,
               "compiled three-slot plan layout changed");

uint8_t note_fx_plan_compile(const note_fx_track_state_t *effective,
                             note_fx_compiled_plan_t *out_plan);
uint8_t note_fx_plan_model(note_fx_slot_plan_word_t word);
uint8_t note_fx_plan_param(note_fx_slot_plan_word_t word, uint8_t param);
uint8_t note_fx_plan_slot_at(uint8_t order, uint8_t position);
uint8_t note_fx_plan_position_of(uint8_t order, uint8_t slot);

#endif
