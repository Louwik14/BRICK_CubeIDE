#ifndef APP_HALL_HALL_NOTE_MIDI_H
#define APP_HALL_HALL_NOTE_MIDI_H

#include <stddef.h>
#include <stdint.h>

#define HALL_NOTE_MIDI_SENSOR_COUNT 16U
#define HALL_NOTE_MIDI_BASE_NOTE    48U
#define HALL_NOTE_MIDI_CHANNEL      0U

typedef void (*hall_note_midi_emit_fn)(void *context,
                                       const uint8_t *msg,
                                       size_t len);

typedef struct
{
    uint8_t sensor_active[HALL_NOTE_MIDI_SENSOR_COUNT];
} hall_note_midi_t;

void hall_note_midi_init(hall_note_midi_t *mapper);
void hall_note_midi_reset(hall_note_midi_t *mapper,
                          hall_note_midi_emit_fn emit,
                          void *context);
void hall_note_midi_update_sensor(hall_note_midi_t *mapper,
                                  uint8_t sensor_index,
                                  uint8_t active,
                                  uint8_t velocity,
                                  hall_note_midi_emit_fn emit,
                                  void *context);
void hall_note_midi_update_array(hall_note_midi_t *mapper,
                                 const uint8_t *active_states,
                                 const uint8_t *velocities,
                                 hall_note_midi_emit_fn emit,
                                 void *context);
uint8_t hall_note_midi_note_for_sensor(uint8_t sensor_index);

#endif /* APP_HALL_HALL_NOTE_MIDI_H */
