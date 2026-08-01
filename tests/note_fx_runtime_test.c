#include <assert.h>
#include <stdint.h>

#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"

static uint32_t g_terminal_count;

uint8_t track_topology_is_play(uint8_t track) { return track < NOTE_FX_TRACK_COUNT; }
uint8_t track_runtime_get_midi_channel_zero_based(uint8_t track) { return track & 0x0FU; }
uint64_t seq_runtime_exec_get_audio_timeline_sample(void) { return 0U; }
void seq_play_scheduler_dispatch_terminal_note_to_channel(uint8_t track,
                                                          uint8_t channel,
                                                          uint8_t note,
                                                          uint8_t velocity,
                                                          uint8_t is_note_on)
{
    (void)track;
    (void)channel;
    (void)note;
    (void)velocity;
    (void)is_note_on;
    ++g_terminal_count;
}

int main(void)
{
    note_fx_state_init();
    note_fx_pipeline_init();

    assert(note_fx_pipeline_apply_runtime_param(0U, 1U, 3U, NOTE_FX_MODEL_ARP));
    assert(note_fx_pipeline_apply_runtime_param(0U, 2U, 3U, NOTE_FX_MODEL_OFF));
    assert(note_fx_pipeline_release_runtime_param(0U, 2U, 3U));
    assert(note_fx_pipeline_submit(0U, 60U, 100U, 1U, 0U));
    assert(g_terminal_count == 0U);

    assert(note_fx_pipeline_release_runtime_param(0U, 1U, 3U));
    assert(note_fx_pipeline_submit(0U, 64U, 100U, 1U, 0U));
    assert(g_terminal_count == 1U);

    note_fx_track_state_t restored = {0};
    restored.value[0][0] = 99U;
    restored.value[0][1] = 99U;
    restored.value[0][2] = 0U;
    restored.value[0][3] = NOTE_FX_MODEL_ARP;
    restored.value[1][3] = NOTE_FX_MODEL_ARP;
    assert(note_fx_state_restore_track(0U, &restored));

    float value = 0.0f;
    assert(note_fx_state_get_param(0U, PARAM_MIDI_FX_S1_PARAM1, &value) && value == 2.0f);
    assert(note_fx_state_get_param(0U, PARAM_MIDI_FX_S1_PARAM2, &value) && value == 0.0f);
    assert(note_fx_state_get_param(0U, PARAM_MIDI_FX_S1_PARAM3, &value) && value == 1.0f);
    assert(note_fx_state_get_param(0U, PARAM_MIDI_FX_S1_MODEL, &value) && value == NOTE_FX_MODEL_ARP);
    assert(note_fx_state_get_param(0U, PARAM_MIDI_FX_S2_MODEL, &value) && value == NOTE_FX_MODEL_OFF);
    return 0;
}
