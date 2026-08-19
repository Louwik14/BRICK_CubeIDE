#ifndef CONTROL_AUDIO_QUEUE_H
#define CONTROL_AUDIO_QUEUE_H

#include <stdint.h>

#include "Core/entity_topology.h"

/* Transitional storage ceiling inherited from the scheduler queue. It is not
 * the final inter-core sizing contract. */
#define CONTROL_AUDIO_QUEUE_CAPACITY 512U

typedef enum
{
    CONTROL_AUDIO_EVENT_NOTE_OFF = 0,
    CONTROL_AUDIO_EVENT_NOTE_ON,
    CONTROL_AUDIO_EVENT_PARAM_SET,
    CONTROL_AUDIO_EVENT_PARAM_RESTORE,
    CONTROL_AUDIO_EVENT_BOUNDARY_EDGE,
    CONTROL_AUDIO_EVENT_METRONOME_CLICK,
    CONTROL_AUDIO_EVENT_CLOSE_ENTITY,
    CONTROL_AUDIO_EVENT_CLOSE_ALL,
    CONTROL_AUDIO_EVENT_BINDING_INTENT,
    CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_START,
    CONTROL_AUDIO_EVENT_LOOPER_TRANSPORT_STOP,
    CONTROL_AUDIO_EVENT_LOOPER_RECORD_STOP,
    CONTROL_AUDIO_EVENT_LOOPER_PREPARE_REPLACE,
    CONTROL_AUDIO_EVENT_LOOPER_RECORD_START,
    CONTROL_AUDIO_EVENT_MULTI_STOP
} control_audio_event_kind_t;

typedef enum
{
    CONTROL_AUDIO_PROVENANCE_STEP = 0,
    CONTROL_AUDIO_PROVENANCE_KEY,
    CONTROL_AUDIO_PROVENANCE_MIDI,
    CONTROL_AUDIO_PROVENANCE_FX
} control_audio_provenance_t;

typedef struct
{
    uint64_t due_sample;
    uint32_t binding_generation;
    uint32_t occurrence_token;
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

_Static_assert(sizeof(control_audio_event_t) == 32U,
               "CONTROL/AUDIO event ABI must remain fixed and pointer-free");

void control_audio_queue_init(void);

/* CONTROL producer. Events must be published in nondecreasing due_sample
 * order. Batch publication is atomic and is the note On/Off pair seam. */
uint8_t control_audio_queue_publish(const control_audio_event_t *event);
uint8_t control_audio_queue_publish_batch(const control_audio_event_t *events,
                                          uint16_t count);

/* AUDIO consumer. Peek does not remove the event; pop commits consumption. */
uint8_t control_audio_queue_audio_peek(control_audio_event_t *out_event);
uint8_t control_audio_queue_audio_pop(void);
uint16_t control_audio_queue_audio_pending_count(void);
uint16_t control_audio_queue_audio_frames_until_due(uint64_t sample_now,
                                                    uint16_t max_frames);

#endif /* CONTROL_AUDIO_QUEUE_H */
