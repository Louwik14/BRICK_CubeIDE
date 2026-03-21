#include "App/Hall/hall_juno_midi.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_note_midi.h"
#include "Audio/microdexed_synth.h"

void hall_juno_midi_init(void)
{
    microdexed_synth_all_notes_off();
}

void hall_juno_midi_process(void)
{
    uint8_t key;

    for (key = 0U; key < HALL_KEY_COUNT; key++)
    {
        const uint8_t note = hall_note_midi_note_for_sensor(key);

        if (hall_engine_consume_note_on(key) != 0U)
        {
            uint8_t velocity = hall_engine_get_velocity(key);

            if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
            {
                velocity = 100U;
            }

            microdexed_synth_note_on(note, velocity);
        }

        if (hall_engine_consume_note_off(key) != 0U)
        {
            microdexed_synth_note_off(note);
        }
    }
}
