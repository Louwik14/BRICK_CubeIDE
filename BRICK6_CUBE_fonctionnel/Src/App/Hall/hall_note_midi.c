#include "App/Hall/hall_note_midi.h"

#include <string.h>

static void hall_note_midi_emit_note(hall_note_midi_emit_fn emit,
                                     void *context,
                                     uint8_t status,
                                     uint8_t note,
                                     uint8_t value)
{
    uint8_t msg[3];

    if (emit == 0)
    {
        return;
    }

    msg[0] = status;
    msg[1] = note;
    msg[2] = value;
    emit(context, msg, sizeof(msg));
}

void hall_note_midi_init(hall_note_midi_t *mapper)
{
    if (mapper == 0)
    {
        return;
    }

    memset(mapper, 0, sizeof(*mapper));
}

uint8_t hall_note_midi_note_for_sensor(uint8_t sensor_index)
{
    if (sensor_index >= HALL_NOTE_MIDI_SENSOR_COUNT)
    {
        return HALL_NOTE_MIDI_BASE_NOTE;
    }

    return (uint8_t)(HALL_NOTE_MIDI_BASE_NOTE + sensor_index);
}

void hall_note_midi_reset(hall_note_midi_t *mapper,
                          hall_note_midi_emit_fn emit,
                          void *context)
{
    uint8_t sensor_index;

    if (mapper == 0)
    {
        return;
    }

    for (sensor_index = 0U; sensor_index < HALL_NOTE_MIDI_SENSOR_COUNT; sensor_index++)
    {
        if (mapper->sensor_active[sensor_index] != 0U)
        {
            hall_note_midi_emit_note(emit,
                                     context,
                                     (uint8_t)(0x80U | HALL_NOTE_MIDI_CHANNEL),
                                     hall_note_midi_note_for_sensor(sensor_index),
                                     0U);
        }
    }

    memset(mapper->sensor_active, 0, sizeof(mapper->sensor_active));
}

void hall_note_midi_update_sensor(hall_note_midi_t *mapper,
                                  uint8_t sensor_index,
                                  uint8_t active,
                                  uint8_t velocity,
                                  hall_note_midi_emit_fn emit,
                                  void *context)
{
    uint8_t was_active;
    uint8_t note;

    if ((mapper == 0) || (sensor_index >= HALL_NOTE_MIDI_SENSOR_COUNT))
    {
        return;
    }

    note = hall_note_midi_note_for_sensor(sensor_index);
    was_active = mapper->sensor_active[sensor_index];

    active = (active != 0U) ? 1U : 0U;

    if (velocity == 0U)
    {
        velocity = 1U;
    }

    if (was_active == active)
    {
        return;
    }

    mapper->sensor_active[sensor_index] = active;

    if (active != 0U)
    {
        hall_note_midi_emit_note(emit,
                                 context,
                                 (uint8_t)(0x90U | HALL_NOTE_MIDI_CHANNEL),
                                 note,
                                 velocity);
    }
    else
    {
        hall_note_midi_emit_note(emit,
                                 context,
                                 (uint8_t)(0x80U | HALL_NOTE_MIDI_CHANNEL),
                                 note,
                                 0U);
    }
}

void hall_note_midi_update_array(hall_note_midi_t *mapper,
                                 const uint8_t *active_states,
                                 const uint8_t *velocities,
                                 hall_note_midi_emit_fn emit,
                                 void *context)
{
    uint8_t sensor_index;

    if ((mapper == 0) || (active_states == 0) || (velocities == 0))
    {
        return;
    }

    for (sensor_index = 0U; sensor_index < HALL_NOTE_MIDI_SENSOR_COUNT; sensor_index++)
    {
        hall_note_midi_update_sensor(mapper,
                                     sensor_index,
                                     active_states[sensor_index],
                                     velocities[sensor_index],
                                     emit,
                                     context);
    }
}
