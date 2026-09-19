#include "NoteFx/note_fx_plan.h"

#include <stddef.h>
#include <string.h>

static uint8_t note_fx_plan_flags_for_model(uint8_t model)
{
    switch ((note_fx_model_t)model)
    {
        case NOTE_FX_MODEL_ARP_FREE:
        case NOTE_FX_MODEL_ARP_SYNC:
        case NOTE_FX_MODEL_EUCLID:
            return (uint8_t)(NOTE_FX_PLAN_FLAG_TEMPORAL
                | NOTE_FX_PLAN_FLAG_HELD);
        case NOTE_FX_MODEL_ECHO:
            return NOTE_FX_PLAN_FLAG_TEMPORAL;
        case NOTE_FX_MODEL_HARMONIZER:
            return (uint8_t)(NOTE_FX_PLAN_FLAG_MODIFIER
                | NOTE_FX_PLAN_FLAG_FAN_OUT);
        case NOTE_FX_MODEL_PROBABILITY:
        case NOTE_FX_MODEL_GATE:
        case NOTE_FX_MODEL_CHORD:
            return NOTE_FX_PLAN_FLAG_MODIFIER;
        case NOTE_FX_MODEL_GROOVE:
            return (uint8_t)(NOTE_FX_PLAN_FLAG_MODIFIER
                | NOTE_FX_PLAN_FLAG_TEMPORAL);
        case NOTE_FX_MODEL_OFF:
        case NOTE_FX_MODEL_COUNT:
        default:
            return 0U;
    }
}

static note_fx_slot_plan_word_t note_fx_plan_pack(uint8_t model,
                                                   uint8_t flags,
                                                   uint8_t p1,
                                                   uint8_t p2,
                                                   uint8_t p3)
{
    return (note_fx_slot_plan_word_t)(model & 0x0FU)
        | ((note_fx_slot_plan_word_t)(flags & 0x0FU) << 4U)
        | ((note_fx_slot_plan_word_t)p1 << 8U)
        | ((note_fx_slot_plan_word_t)p2 << 16U)
        | ((note_fx_slot_plan_word_t)p3 << 24U);
}

uint8_t note_fx_plan_compile(const note_fx_track_state_t *effective,
                             uint16_t override_mask,
                             note_fx_compiled_plan_t *out_plan)
{
    if ((effective == NULL) || (out_plan == NULL)
            || (note_fx_state_validate_unique_families(effective) == 0U))
        return 0U;

    memset(out_plan, 0, sizeof(*out_plan));
    out_plan->override_mask = override_mask;
    out_plan->first_active_slot = NOTE_FX_PLAN_FIRST_SLOT_NONE;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const uint8_t model =
            effective->value[slot][NOTE_FX_PARAM_COUNT - 1U];
        if (model >= NOTE_FX_MODEL_COUNT) return 0U;
        const uint8_t flags = note_fx_plan_flags_for_model(model);
        out_plan->slot[slot] = note_fx_plan_pack(model, flags,
            effective->value[slot][0U], effective->value[slot][1U],
            effective->value[slot][2U]);
        if (model != NOTE_FX_MODEL_OFF)
        {
            out_plan->active_mask |= (uint8_t)(1U << slot);
            if (out_plan->first_active_slot == NOTE_FX_PLAN_FIRST_SLOT_NONE)
                out_plan->first_active_slot = slot;
        }
    }
    return 1U;
}

uint8_t note_fx_plan_model(note_fx_slot_plan_word_t word)
{
    return (uint8_t)(word & 0x0FU);
}

uint8_t note_fx_plan_flags(note_fx_slot_plan_word_t word)
{
    return (uint8_t)((word >> 4U) & 0x0FU);
}

uint8_t note_fx_plan_param(note_fx_slot_plan_word_t word, uint8_t param)
{
    return (param < (NOTE_FX_PARAM_COUNT - 1U))
        ? (uint8_t)(word >> (8U * (uint32_t)(param + 1U))) : 0U;
}
