#ifndef CONTROL_AUDIO_QUEUE_H
#define CONTROL_AUDIO_QUEUE_H

#include <stdint.h>

#include "Core/entity_topology.h"

/* Dated non-musical CONTROL -> AUDIO command transport. */
#define CONTROL_AUDIO_QUEUE_CAPACITY 128U

typedef enum
{
    CONTROL_AUDIO_EVENT_BOUNDARY_EDGE = 0,
    CONTROL_AUDIO_EVENT_METRONOME_CLICK,
    CONTROL_AUDIO_EVENT_BINDING_INTENT,
    CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_START,
    CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_STOP,
    CONTROL_AUDIO_EVENT_LOOPER_RECORD_STOP,
    CONTROL_AUDIO_EVENT_LOOPER_PREPARE_REPLACE,
    CONTROL_AUDIO_EVENT_LOOPER_RECORD_START,
    CONTROL_AUDIO_EVENT_MULTI_STOP,
    CONTROL_AUDIO_EVENT_RAM_STOP,
    CONTROL_AUDIO_EVENT_WAVETABLE_STOP
} control_audio_event_kind_t;

typedef struct
{
    uint64_t due_sample;
    uint32_t source_generation;
    uint16_t param_id;
    uint16_t param_value;
    brick_entity_id_t entity_id;
    uint8_t kind;
    uint8_t note;
    uint8_t velocity;
    uint8_t provenance;
    uint8_t flags;
} control_audio_event_t;

_Static_assert(sizeof(control_audio_event_t) == 24U,
               "CONTROL/AUDIO event ABI must remain fixed and pointer-free");

void control_audio_queue_init(void);

/* CONTROL producer for dated non-musical commands. */
uint8_t control_audio_queue_publish(const control_audio_event_t *event);

/* AUDIO consumer. Peek does not remove the event; pop commits consumption. */
uint8_t control_audio_queue_audio_peek(control_audio_event_t *out_event);
uint8_t control_audio_queue_audio_pop(void);
uint16_t control_audio_queue_audio_pending_count(void);
uint16_t control_audio_queue_audio_frames_until_due(uint64_t sample_now,
                                                    uint16_t max_frames);

#endif /* CONTROL_AUDIO_QUEUE_H */
