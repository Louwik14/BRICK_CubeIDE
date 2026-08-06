#ifndef BRICK6_LIVE_EVENT_H
#define BRICK6_LIVE_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    LIVE_EVENT_SOURCE_HALL = 0,
    LIVE_EVENT_SOURCE_MIDI_DEVICE = 1,
    LIVE_EVENT_SOURCE_MIDI_HOST = 2
} live_event_source_t;

/* Fixed-size, pointer-free contract suitable for a future shared M4/M7 RAM. */
typedef struct
{
    uint32_t tim5_tick;
    uint32_t ingress_serial;
    uint32_t occurrence_id;
    uint8_t key;
    uint8_t pressed;
    uint8_t velocity;
    uint8_t source;
} live_event_t;

typedef enum
{
    LIVE_NOTE_EVENT_OFF = 0,
    LIVE_NOTE_EVENT_ON = 1
} live_note_event_type_t;

/* Audio-owned timed queue item. It is fixed-size, pointer-free and therefore
 * directly transferable to a future shared M4/M7 event queue. */
typedef struct
{
    uint64_t sample_time;
    uint32_t ingress_serial;
    uint32_t occurrence_id;
    uint8_t type;
    uint8_t source;
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
} live_note_event_t;

_Static_assert(sizeof(live_note_event_t) == 24U,
               "live_note_event_t must remain a fixed 24-byte event");

_Static_assert(sizeof(live_event_t) == 16U,
               "live_event_t must remain a fixed 16-byte shared event");

#define LIVE_EVENT_QUEUE_CAPACITY 64U

void live_event_init(void);

bool live_event_submit_from_hall(uint8_t key,
                                 bool pressed,
                                 uint8_t velocity,
                                 uint32_t tim5_tick);

bool live_event_pop(live_event_t *out_event);
uint16_t live_event_depth(void);
uint32_t live_event_drop_count(void);

#endif /* BRICK6_LIVE_EVENT_H */
