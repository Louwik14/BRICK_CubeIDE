#include "midi.h"
#include "Audio/juno_midi_queue.h"

static void juno_midi_push_event(uint8_t type,
                                 uint8_t channel,
                                 uint8_t data1,
                                 uint8_t data2,
                                 int16_t value)
{
    juno_midi_event_t event;
    event.type = type;
    event.channel = channel;
    event.data1 = data1;
    event.data2 = data2;
    event.value = value;
    (void)juno_midi_queue_push(&event);
}

void midi_internal_receive(const uint8_t *msg, size_t len)
{
    if((msg == NULL) || (len == 0U))
        return;

    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0U;
    const uint8_t channel = status & 0x0FU;

    switch(type)
    {
        case 0x80U:
            if(len >= 3U)
                juno_midi_push_event(JUNO_MIDI_EVENT_NOTE_OFF, channel, msg[1], msg[2], 0);
            break;

        case 0x90U:
            if(len >= 3U)
            {
                if(msg[2] == 0U)
                    juno_midi_push_event(JUNO_MIDI_EVENT_NOTE_OFF, channel, msg[1], 0U, 0);
                else
                    juno_midi_push_event(JUNO_MIDI_EVENT_NOTE_ON, channel, msg[1], msg[2], 0);
            }
            break;

        case 0xB0U:
            if(len >= 3U)
            {
                if((msg[1] == 123U) || (msg[1] == 120U))
                    juno_midi_push_event(JUNO_MIDI_EVENT_ALL_NOTES_OFF, channel, msg[1], msg[2], 0);
                else if(msg[1] == 64U)
                    juno_midi_push_event(JUNO_MIDI_EVENT_CC, channel, msg[1], msg[2], 0);
            }
            break;

        case 0xE0U:
            if(len >= 3U)
            {
                const int16_t bend = (int16_t)(((uint16_t)msg[1]) | (((uint16_t)msg[2]) << 7)) - 8192;
                juno_midi_push_event(JUNO_MIDI_EVENT_PITCH_BEND, channel, 0U, 0U, bend);
            }
            break;

        default:
            break;
    }
}
