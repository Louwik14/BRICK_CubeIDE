#include <stdint.h>

#include "Core/synth_polyphony.h"

void brick6_braids_runtime_all_notes_off(uint8_t instance_id) { (void)instance_id; }
void brick6_braids_runtime_clear_trigger(uint8_t instance_id) { (void)instance_id; }
void brick6_braids_runtime_reset_instance(uint8_t instance_id) { (void)instance_id; }
void brick6_stack_runtime_all_notes_off(uint8_t instance_id) { (void)instance_id; }
void brick6_stack_runtime_clear_trigger(uint8_t instance_id) { (void)instance_id; }
void brick6_stack_runtime_reset_instance(uint8_t instance_id) { (void)instance_id; }
void brick6_wave_runtime_all_notes_off(uint8_t instance_id) { (void)instance_id; }
void brick6_wave_runtime_clear_trigger(uint8_t instance_id) { (void)instance_id; }
void brick6_wave_runtime_reset_instance(uint8_t instance_id) { (void)instance_id; }
void brick6_deluge_runtime_all_notes_off(uint8_t instance_id) { (void)instance_id; }
void brick6_deluge_runtime_reset_instance(uint8_t instance_id) { (void)instance_id; }
void mixer_track_poly_all_notes_off(uint32_t poly_track_id) { (void)poly_track_id; }
void mixer_synth_voice_slot_reset(uint8_t slot) { (void)slot; }

static int unique_slots(void)
{
    uint8_t seen[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET] = { 0U };
    for (uint8_t track = 0U; track < SYNTH_POLYPHONY_TRACK_CAPACITY; ++track)
    {
        if (synth_polyphony_get_track_active(track) == 0U) continue;
        const uint8_t count = synth_polyphony_get_voice_count(track);
        for (uint8_t voice = 0U; voice < count; ++voice)
        {
            const uint8_t slot = synth_polyphony_get_slot(track, voice);
            if ((slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) || (seen[slot] != 0U)) return 0;
            seen[slot] = 1U;
        }
    }
    return 1;
}

int main(void)
{
    synth_polyphony_init();
    if (synth_polyphony_get_free_count() != 16U) return 1;
    if (synth_polyphony_set_track_active(0U, 1U, 1U) == 0U) return 2;
    if (synth_polyphony_set_voice_count(0U, 8U) != 8U) return 3;
    if (synth_polyphony_get_renderable_voice_mask(0U) != 0U) return 14;
    const uint8_t voice0 = synth_polyphony_note_on_from(0U, 60U, SYNTH_POLY_SOURCE_MANUAL);
    const uint8_t voice1 = synth_polyphony_note_on_from(0U, 61U, SYNTH_POLY_SOURCE_SEQUENCER);
    if ((voice0 != 0U) || (voice1 != 1U)) return 15;
    if (synth_polyphony_get_renderable_voice_mask(0U) != 0x03U) return 16;
    if ((synth_polyphony_voice_is_renderable(0U, voice0) == 0U)
            || (synth_polyphony_voice_is_renderable(0U, voice1) == 0U)) return 17;
    if (synth_polyphony_note_off_from(0U, 60U, SYNTH_POLY_SOURCE_MANUAL) != voice0) return 18;
    if (synth_polyphony_get_renderable_voice_mask(0U) != 0x03U) return 19;
    synth_polyphony_voice_release_complete(0U, voice0);
    if (synth_polyphony_get_renderable_voice_mask(0U) != 0x02U) return 20;
    if (synth_polyphony_note_on_from(0U, 62U, SYNTH_POLY_SOURCE_MANUAL) != voice0) return 21;
    synth_poly_release_t released[2];
    if (synth_polyphony_release_all(0U, released, 2U) != 2U) return 22;
    synth_polyphony_voice_release_complete(0U, released[0].voice);
    synth_polyphony_voice_release_complete(0U, released[1].voice);
    if (synth_polyphony_get_renderable_voice_mask(0U) != 0U) return 23;
    if (synth_polyphony_set_track_active(1U, 1U, 2U) == 0U) return 4;
    if (synth_polyphony_set_voice_count(1U, 8U) != 8U) return 5;
    if ((synth_polyphony_get_free_count() != 0U) || (unique_slots() == 0)) return 6;
    if (synth_polyphony_set_track_active(2U, 1U, 3U) != 0U) return 7;
    if (synth_polyphony_set_voice_count(0U, 4U) != 4U) return 8;
    if (synth_polyphony_get_free_count() != 4U) return 9;
    if (synth_polyphony_set_track_active(2U, 1U, 3U) == 0U) return 10;
    if (synth_polyphony_set_voice_count(2U, 8U) != 3U) return 11;
    if ((synth_polyphony_get_free_count() != 0U) || (unique_slots() == 0)) return 12;
    (void)synth_polyphony_set_track_active(1U, 0U, 0U);
    if (synth_polyphony_get_free_count() != 8U) return 13;
    if (synth_polyphony_get_renderable_voice_mask(1U) != 0U) return 24;
    return 0;
}
