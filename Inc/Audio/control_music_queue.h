#ifndef CONTROL_MUSIC_QUEUE_H
#define CONTROL_MUSIC_QUEUE_H

#include <stdint.h>

#include "Core/entity_topology.h"

#define CONTROL_MUSIC_QUEUE_CAPACITY 257U
#define CONTROL_MUSIC_EXTERNAL_QUEUE_CAPACITY 129U
#define CONTROL_MUSIC_TRIGGER_EXTERNAL_FLAG 0x80000000UL
/* 64-frame worst case after CONTROL fan-out and final victim selection.  A ledger
 * starts with at most 8 outputs; lowering polyphony to one contributes at
 * most seven initial victim STOPs.  Every new START then contributes itself
 * and at most one victim/natural STOP (a natural STOP removes one victim). */
#define CONTROL_MUSIC_FULL_ENTITY_HORIZON_STARTS 8U
#define CONTROL_MUSIC_CHILD_HORIZON_STARTS 1U
#define CONTROL_MUSIC_ENTITY_INITIAL_VICTIMS 7U
#define CONTROL_MUSIC_FULL_ENTITY_HORIZON_ACTIONS \
    ((2U * CONTROL_MUSIC_FULL_ENTITY_HORIZON_STARTS) \
        + CONTROL_MUSIC_ENTITY_INITIAL_VICTIMS)
#define CONTROL_MUSIC_CHILD_HORIZON_ACTIONS \
    ((2U * CONTROL_MUSIC_CHILD_HORIZON_STARTS) \
        + CONTROL_MUSIC_ENTITY_INITIAL_VICTIMS)
#define CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST \
    (((BRICK_ENTITY_TOP_LEVEL_COUNT - 1U) \
        * CONTROL_MUSIC_FULL_ENTITY_HORIZON_ACTIONS) \
     + (BRICK_ENTITY_GROUP_CHILD_COUNT \
        * CONTROL_MUSIC_CHILD_HORIZON_ACTIONS))

_Static_assert(CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST == 233U,
               "musical horizon proof changed with entity topology");

_Static_assert((CONTROL_MUSIC_QUEUE_CAPACITY - 1U)
                   >= CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST,
               "internal musical ingress must cover the complete 64-frame burst");

typedef enum
{
    CONTROL_MUSIC_ACTION_STOP = 0,
    CONTROL_MUSIC_ACTION_START,
    CONTROL_MUSIC_ACTION_RETRIGGER
} control_music_action_kind_t;

typedef struct
{
    uint64_t due_sample;
    uint32_t binding_generation;
    uint32_t output_id;
    uint32_t trigger_id;
    brick_entity_id_t entity_id;
    uint8_t kind;
    uint8_t note;
    uint8_t velocity;
} control_music_action_t;

_Static_assert(sizeof(control_music_action_t) == 24U,
               "CONTROL/AUDIO music action ABI must remain fixed and pointer-free");

void control_music_queue_init(void);
void control_music_queue_request_panic(void);
uint8_t control_music_queue_audio_consume_panic(void);
uint8_t control_music_queue_publish(const control_music_action_t *action);
uint16_t control_music_queue_control_free(uint8_t external);
uint8_t control_music_queue_publish_batch(const control_music_action_t *actions,
                                          uint16_t count);
uint8_t control_music_queue_publish_ordered_window(
    const control_music_action_t *actions,
    const uint16_t *next_indices,
    const uint16_t *bucket_heads,
    uint16_t bucket_count,
    uint16_t action_count,
    uint8_t external);
uint8_t control_music_queue_audio_peek(control_music_action_t *out_action);
uint8_t control_music_queue_audio_pop(const control_music_action_t *consumed);
uint16_t control_music_queue_audio_pending_count(void);
uint16_t control_music_queue_audio_frames_until_due(uint64_t sample_now,
                                                    uint16_t max_frames);

#endif /* CONTROL_MUSIC_QUEUE_H */
