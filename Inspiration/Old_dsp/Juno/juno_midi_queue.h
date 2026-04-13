#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    JUNO_MIDI_EVENT_NOTE_ON = 0,
    JUNO_MIDI_EVENT_NOTE_OFF,
    JUNO_MIDI_EVENT_PITCH_BEND,
    JUNO_MIDI_EVENT_ALL_NOTES_OFF,
    JUNO_MIDI_EVENT_CC
} juno_midi_event_type_t;

typedef struct
{
    uint8_t type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
    int16_t value;
} juno_midi_event_t;

void juno_midi_queue_init(void);
void juno_midi_queue_clear(void);
uint8_t juno_midi_queue_push(const juno_midi_event_t *event);
uint8_t juno_midi_queue_pop(juno_midi_event_t *event);
uint32_t juno_midi_queue_drop_count(void);

#ifdef __cplusplus
}
#endif
