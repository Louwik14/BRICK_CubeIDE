#include "NoteFx/note_fx_plan.h"

#include <stddef.h>
#include <string.h>

static const uint8_t g_note_fx_order[NOTE_FX_ORDER_COUNT][NOTE_FX_SLOT_COUNT] =
{
    { 0U, 1U, 2U }, { 0U, 2U, 1U }, { 1U, 0U, 2U },
    { 1U, 2U, 0U }, { 2U, 0U, 1U }, { 2U, 1U, 0U }
};

uint8_t note_fx_plan_compile(const note_fx_track_state_t *effective,
                             note_fx_compiled_plan_t *out_plan)
{
    if ((effective == NULL) || (out_plan == NULL)
            || (note_fx_state_validate_unique_families(effective) == 0U))
        return 0U;

    memset(out_plan, 0, sizeof(*out_plan));
    out_plan->order = (effective->order < NOTE_FX_ORDER_COUNT)
        ? effective->order : 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const uint8_t model =
            effective->value[slot][NOTE_FX_MODEL_INDEX];
        if (model >= NOTE_FX_MODEL_COUNT) return 0U;
        out_plan->slot[slot].model = model;
        memcpy(out_plan->slot[slot].param, effective->value[slot],
               NOTE_FX_PARAM_COUNT);
    }
    return 1U;
}

uint8_t note_fx_plan_model(note_fx_slot_plan_word_t word)
{
    return word.model;
}

uint8_t note_fx_plan_param(note_fx_slot_plan_word_t word, uint8_t param)
{
    return (param < NOTE_FX_PARAM_COUNT) ? word.param[param] : 0U;
}

uint8_t note_fx_plan_slot_at(uint8_t order, uint8_t position)
{
    return ((order < NOTE_FX_ORDER_COUNT) && (position < NOTE_FX_SLOT_COUNT))
        ? g_note_fx_order[order][position] : NOTE_FX_SLOT_COUNT;
}

uint8_t note_fx_plan_position_of(uint8_t order, uint8_t slot)
{
    if ((order >= NOTE_FX_ORDER_COUNT) || (slot >= NOTE_FX_SLOT_COUNT))
        return NOTE_FX_SLOT_COUNT;
    for (uint8_t position = 0U; position < NOTE_FX_SLOT_COUNT; ++position)
        if (g_note_fx_order[order][position] == slot) return position;
    return NOTE_FX_SLOT_COUNT;
}
