#include "App/Hall/hall_juno_midi.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_note_midi.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "ui_core.h"

static ui_track_type_t hall_juno_midi_get_runtime_synth_type(void)
{
    return ui_get_track_type(UI_AUDIO_INPUT_RESOURCE_COUNT);
}

void hall_juno_midi_init(void)
{
    microdexed_synth_all_notes_off();
    monob_synth_all_notes_off();
}

void hall_juno_midi_process(void)
{
    uint8_t key;

    for (key = 0U; key < HALL_KEY_COUNT; key++)
    {
        const uint8_t note = hall_note_midi_note_for_sensor(key);

        if ((key < UI_TRACK_COUNT) && (ui_core_hall_note_is_suppressed(key) != 0U))
        {
            (void)hall_engine_consume_note_on(key);

            if (hall_engine_consume_note_off(key) != 0U)
            {
                ui_core_clear_hall_note_suppression(key);
            }

            continue;
        }

        if (hall_engine_consume_note_on(key) != 0U)
        {
            uint8_t velocity = hall_engine_get_velocity(key);

            if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
            {
                velocity = 100U;
            }

            if (hall_juno_midi_get_runtime_synth_type() == UI_TRACK_TYPE_MONOB)
            {
                monob_synth_note_on(note, velocity);
            }
            else
            {
                microdexed_synth_note_on(note, velocity);
            }
        }

        if (hall_engine_consume_note_off(key) != 0U)
        {
            if (hall_juno_midi_get_runtime_synth_type() == UI_TRACK_TYPE_MONOB)
            {
                monob_synth_note_off(note);
            }
            else
            {
                microdexed_synth_note_off(note);
            }
        }
    }
}
