#ifndef NOTE_FX_EVENT_H
#define NOTE_FX_EVENT_H

#include <stdint.h>

/* Fixed sample-domain contract shared by sources, FX stages and the terminal. */
typedef enum
{
    NOTE_EVENT_KIND_OFF = 0,
    NOTE_EVENT_KIND_ON = 1
} note_event_kind_t;

typedef enum
{
    NOTE_EVENT_SOURCE_KEY = 0,
    NOTE_EVENT_SOURCE_STEP,
    NOTE_EVENT_SOURCE_MIDI,
    NOTE_EVENT_SOURCE_FX,
    NOTE_EVENT_SOURCE_COUNT
} note_event_provenance_t;

typedef enum
{
    NOTE_EVENT_RESULT_ACCEPTED = 1,
    NOTE_EVENT_RESULT_REJECTED_CAPACITY,
    NOTE_EVENT_RESULT_REJECTED_STALE,
    NOTE_EVENT_RESULT_REJECTED_DESTINATION,
    NOTE_EVENT_RESULT_DROPPED_POLICY
} note_event_result_t;

#define NOTE_EVENT_STAGE_SOURCE   0U
#define NOTE_EVENT_STAGE_TERMINAL 4U
/* Stage 3 is the hand-off emitted by the third MIDI FX slot. */
#define NOTE_EVENT_STAGE_TERMINAL_HANDOFF (NOTE_EVENT_STAGE_TERMINAL - 1U)
#define NOTE_EVENT_DESTINATION_DEFAULT 0xFFU
#define NOTE_EVENT_OCCURRENCE_COUNTER_MASK 0x3FFFFFFFU
#define NOTE_EVENT_OCCURRENCE_NAMESPACE_STEP 0x00000000U
#define NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY  0x40000000U
#define NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI 0x80000000U
#define NOTE_EVENT_OCCURRENCE_NAMESPACE_FX   0xC0000000U
#define NOTE_EVENT_FLAG_GENERATED 0x01U
#define NOTE_EVENT_FLAG_TERMINAL  0x04U
#define NOTE_EVENT_FLAG_STALE     0x08U

typedef struct
{
    uint64_t sample_abs;
    uint8_t track;
    uint8_t destination_id;
    uint8_t note;
    uint8_t velocity;
    uint8_t kind;
    uint8_t provenance;
    uint8_t stage;
    uint32_t source_token;
    uint32_t occurrence_id;
    uint32_t generation;
    uint8_t flags;
} note_event_t;

_Static_assert(sizeof(note_event_t) == 32U, "note_event_t layout must remain fixed");

/* Existing NoteFx names remain source-compatible while using the canonical layout. */
typedef note_event_t note_fx_event_t;
typedef note_event_kind_t note_fx_event_type_t;
typedef note_event_result_t note_fx_result_t;

static inline uint32_t note_event_occurrence_namespace(
    note_event_provenance_t provenance)
{
    switch (provenance)
    {
        case NOTE_EVENT_SOURCE_KEY: return NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY;
        case NOTE_EVENT_SOURCE_MIDI: return NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI;
        case NOTE_EVENT_SOURCE_FX: return NOTE_EVENT_OCCURRENCE_NAMESPACE_FX;
        case NOTE_EVENT_SOURCE_STEP:
        default: return NOTE_EVENT_OCCURRENCE_NAMESPACE_STEP;
    }
}

static inline uint8_t note_event_is_valid(const note_event_t *event)
{
    return (event != 0)
        && (event->note < 128U)
        && (event->velocity < 128U)
        && (event->kind <= (uint8_t)NOTE_EVENT_KIND_ON)
        && (event->provenance < (uint8_t)NOTE_EVENT_SOURCE_COUNT)
        && (event->stage <= NOTE_EVENT_STAGE_TERMINAL)
        && (event->source_token != 0U)
        && (event->occurrence_id != 0U)
        && (event->generation != 0U);
}

static inline uint8_t note_event_is_terminal_handoff(const note_event_t *event)
{
    return (event != 0)
        && (event->stage >= NOTE_EVENT_STAGE_TERMINAL_HANDOFF);
}

#endif
