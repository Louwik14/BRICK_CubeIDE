#ifndef SEQ_AUDIO_BOUNDARY_H
#define SEQ_AUDIO_BOUNDARY_H

#include <stdint.h>

typedef enum
{
    /* Transition-scoped parameters must reach AUDIO before note release. */
    SEQ_ENGINE_EVENT_TRANSITION_PARAM = 0,
    SEQ_ENGINE_EVENT_NOTE_OFF,
    SEQ_ENGINE_EVENT_PARAM,
    SEQ_ENGINE_EVENT_NOTE_ON,
    SEQ_ENGINE_EVENT_PANIC
} seq_event_kind_t;

typedef enum
{
    SEQ_ENGINE_PARAM_TEMP = 0,
    SEQ_ENGINE_PARAM_CLEAR_TEMP,
    SEQ_ENGINE_PARAM_RESTORE_BASE
} seq_param_semantic_t;

typedef union __attribute__((packed))
{
    struct __attribute__((packed))
    {
        uint32_t occurrence_id;
        uint16_t reserved;
        uint8_t track;
        uint8_t note;
        uint8_t velocity;
        uint8_t logical_slot;
    } note;
    struct __attribute__((packed))
    {
        uint32_t value32;
        uint16_t param_id;
        uint8_t track;
        uint8_t semantic;
        uint8_t reserved[2];
    } param;
} seq_terminal_event_t;

_Static_assert(sizeof(seq_terminal_event_t) == 10U,
               "SEQ terminal payload budget");

void seq_engine_irq_init(void);
void seq_engine_audio_boundary(uint64_t block_start_sample, uint8_t recovering);
uint16_t seq_engine_audio_frames_until_due(uint64_t sample, uint16_t maximum);
uint8_t seq_engine_audio_pop_due(uint64_t sample, uint8_t *out_kind,
                                 seq_terminal_event_t *out_event);
uint16_t seq_engine_audio_track_mask(void);
void seq_engine_audio_force_stop(uint64_t effective_sample,
                                 uint8_t preserve_live_notes);

#endif
