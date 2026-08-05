#include <assert.h>
#include <stdint.h>

#include "NoteFx/note_fx_state.h"

uint8_t track_topology_is_active(uint8_t track)
{
    return (track < NOTE_FX_TRACK_COUNT) ? 1U : 0U;
}

static void assert_slot_defaults(const note_fx_track_state_t *state,
                                 uint8_t slot,
                                 uint8_t model)
{
    assert(state->value[slot][0U] == 2U);
    assert(state->value[slot][1U] == 0U);
    assert(state->value[slot][2U] == 1U);
    assert(state->value[slot][3U] == model);
}

int main(void)
{
    note_fx_track_state_t state = {0};
    note_fx_track_state_t restored = {0};

    note_fx_state_init();
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        restored.value[slot][0U] = 7U;
        restored.value[slot][1U] = 4U;
        restored.value[slot][2U] = 4U;
        restored.value[slot][3U] = NOTE_FX_MODEL_ARP;
    }
    assert(note_fx_state_restore_track(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        assert_slot_defaults(&state, slot, NOTE_FX_MODEL_ARP);

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        restored.value[slot][0U] = 5U;
        restored.value[slot][1U] = 3U;
        restored.value[slot][2U] = 4U;
        restored.value[slot][3U] = NOTE_FX_MODEL_ARP;
    }
    assert(note_fx_state_restore_track(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        assert(state.value[slot][0U] == 5U);
        assert(state.value[slot][1U] == 3U);
        assert(state.value[slot][2U] == 4U);
        assert(state.value[slot][3U] == NOTE_FX_MODEL_ARP);
    }

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        restored.value[slot][0U] = 7U;
        restored.value[slot][1U] = 4U;
        restored.value[slot][2U] = 4U;
        restored.value[slot][3U] = NOTE_FX_MODEL_OFF;
    }
    assert(note_fx_state_restore_track(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        assert_slot_defaults(&state, slot, NOTE_FX_MODEL_OFF);

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        restored.value[slot][0U] = 6U;
        restored.value[slot][1U] = 2U;
        restored.value[slot][2U] = 3U;
        restored.value[slot][3U] = NOTE_FX_MODEL_OFF;
    }
    assert(note_fx_state_restore_track_exact(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        assert(state.value[slot][0U] == 6U);
        assert(state.value[slot][1U] == 2U);
        assert(state.value[slot][2U] == 3U);
        assert(state.value[slot][3U] == NOTE_FX_MODEL_OFF);
    }

    return 0;
}
