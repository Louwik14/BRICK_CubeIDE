#include <assert.h>
#include <stdint.h>

#include "Core/entity_topology.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_division_catalog.h"

uint8_t entity_topology_is_active(brick_entity_id_t entity_id)
{
    return (entity_id < NOTE_FX_TRACK_COUNT) ? 1U : 0U;
}

static void assert_slot_defaults(const note_fx_track_state_t *state,
                                 uint8_t slot,
                                 uint8_t model)
{
    assert(state->value[slot][0U] == SEQ_DIVISION_ARP_DEFAULT_INDEX);
    assert(state->value[slot][1U] == 0U);
    assert(state->value[slot][2U] == 1U);
    assert(state->value[slot][3U] == model);
}

static void assert_euclid_defaults(const note_fx_track_state_t *state, uint8_t slot)
{
    assert(state->value[slot][0U] == NOTE_FX_EUCLID_LENGTH_DEFAULT);
    assert(state->value[slot][1U] == NOTE_FX_EUCLID_PULSE_DEFAULT);
    assert(state->value[slot][2U] == SEQ_DIVISION_ARP_DEFAULT_INDEX);
    assert(state->value[slot][3U] == NOTE_FX_MODEL_EUCLID);
}

int main(void)
{
    note_fx_track_state_t state = {0};
    note_fx_track_state_t restored = {0};

    note_fx_state_init();
    assert(note_fx_state_is_param_plock_allowed(
               NOTE_FX_MODEL_EUCLID, 0U) == 0U);
    assert(note_fx_state_is_param_plock_allowed(
               NOTE_FX_MODEL_EUCLID, 1U) == 0U);
    assert(note_fx_state_is_param_plock_allowed(
               NOTE_FX_MODEL_EUCLID, 2U) == 0U);
    assert(note_fx_state_is_param_plock_allowed(
               NOTE_FX_MODEL_EUCLID, 3U) != 0U);
    assert(note_fx_state_is_param_plock_allowed(
               NOTE_FX_MODEL_ARP, 0U) != 0U);
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

    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_MODEL,
                                   (float)NOTE_FX_MODEL_EUCLID) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    assert_euclid_defaults(&state, 0U);
    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_PARAM1, 8.0f) != 0U);
    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_PARAM2, 12.0f) != 0U);
    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_PARAM2, 0.0f) != 0U);
    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_PARAM3, 99.0f) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    assert(state.value[0U][0U] == 8U);
    assert(state.value[0U][1U] == 0U);
    assert(state.value[0U][2U] == SEQ_DIVISION_ARP_DEFAULT_INDEX);
    assert(state.value[0U][3U] == NOTE_FX_MODEL_EUCLID);

    restored.value[0U][0U] = 8U;
    restored.value[0U][1U] = 255U;
    restored.value[0U][2U] = 255U;
    restored.value[0U][3U] = NOTE_FX_MODEL_EUCLID;
    assert(note_fx_state_restore_track(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    assert(state.value[0U][0U] == 8U);
    assert(state.value[0U][1U] == 8U);
    assert(state.value[0U][2U] == SEQ_DIVISION_ARP_DEFAULT_INDEX);
    assert(state.value[0U][3U] == NOTE_FX_MODEL_EUCLID);

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        restored.value[slot][0U] = 32U;
        restored.value[slot][1U] = 0U;
        restored.value[slot][2U] = 7U;
        restored.value[slot][3U] = NOTE_FX_MODEL_EUCLID;
    }
    assert(note_fx_state_restore_track(0U, &restored) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    assert(state.value[0U][0U] == 32U);
    assert(state.value[0U][1U] == 0U);
    assert(state.value[0U][2U] == 7U);

    assert(note_fx_state_set_param(0U, PARAM_MIDI_FX_S1_MODEL,
                                   (float)NOTE_FX_MODEL_ARP) != 0U);
    assert(note_fx_state_capture_track(0U, &state) != 0U);
    assert_slot_defaults(&state, 0U, NOTE_FX_MODEL_ARP);

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
