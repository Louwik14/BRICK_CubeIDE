#include "App/Hall/hall_juno_midi.h"

#include "App/Hall/hall_engine.h"
#include "App/Hall/hall_note_midi.h"
#include "midi.h"

static hall_note_midi_t g_hall_note_midi;

static void hall_juno_midi_emit_internal(void *context,
                                         const uint8_t *msg,
                                         size_t len)
{
    (void)context;
    midi_internal_receive(msg, len);
}

void hall_juno_midi_init(void)
{
    hall_note_midi_init(&g_hall_note_midi);
}

void hall_juno_midi_process(void)
{
    uint8_t key;

    for (key = 0U; key < HALL_KEY_COUNT; key++)
    {
        if (hall_engine_consume_note_on(key) != 0U)
        {
            uint8_t velocity = hall_engine_get_velocity(key);

            if ((hall_engine_get_velocity_valid(key) == 0U) || (velocity == 0U))
            {
                velocity = 100U;
            }

            hall_note_midi_update_sensor(&g_hall_note_midi,
                                         key,
                                         1U,
                                         velocity,
                                         hall_juno_midi_emit_internal,
                                         0);
        }

        if (hall_engine_consume_note_off(key) != 0U)
        {
            hall_note_midi_update_sensor(&g_hall_note_midi,
                                         key,
                                         0U,
                                         0U,
                                         hall_juno_midi_emit_internal,
                                         0);
        }
    }
}
