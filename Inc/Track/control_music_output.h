#ifndef CONTROL_MUSIC_OUTPUT_H
#define CONTROL_MUSIC_OUTPUT_H

#include <stdint.h>

#include "Track/entity_topology.h"

#define CONTROL_MUSIC_ACTION_CAPACITY 384U
#define CONTROL_MUSIC_ACTION_EXTERNAL_FLAG 0x80U
#define CONTROL_MUSIC_ACTION_KIND_MASK 0x03U
#define CONTROL_MUSIC_ACTION_CHANNEL_SHIFT 2U
#define CONTROL_MUSIC_ACTION_CHANNEL_MASK  0x3CU

typedef enum
{
    CONTROL_MUSIC_ACTION_STOP = 0,
    CONTROL_MUSIC_ACTION_START,
    CONTROL_MUSIC_ACTION_RETRIGGER
} control_music_action_kind_t;

typedef struct
{
    uint64_t due_sample;
    uint32_t output_id;
    brick_entity_id_t entity_id;
    uint8_t kind;
    uint8_t note;
    uint8_t velocity;
} control_music_action_t;

_Static_assert(sizeof(control_music_action_t) == 16U,
               "CONTROL note staging action must remain compact");

static inline uint8_t control_music_action_kind(
    const control_music_action_t *action)
{
    return action->kind & CONTROL_MUSIC_ACTION_KIND_MASK;
}

static inline uint8_t control_music_action_is_external(
    const control_music_action_t *action)
{
    return (action->kind & CONTROL_MUSIC_ACTION_EXTERNAL_FLAG) != 0U;
}

static inline uint8_t control_music_action_channel(
    const control_music_action_t *action)
{
    return (uint8_t)((action->kind & CONTROL_MUSIC_ACTION_CHANNEL_MASK)
        >> CONTROL_MUSIC_ACTION_CHANNEL_SHIFT);
}

#define CONTROL_MUSIC_OUTPUTS_PER_ENTITY 8U
#define CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY 2U

typedef void (*control_music_output_death_observer_t)(
    brick_entity_id_t entity_id, uint32_t output_id);

uint8_t control_music_output_register_death_observer(
    control_music_output_death_observer_t observer);

/* A CONTROL horizon is finalized only after scheduler, lifecycle and Note FX
 * have all contributed.  The fixed sample buckets publish the final actions
 * chronologically without adding another persistent transport. */
uint8_t control_music_output_begin_window(uint64_t first_sample,
                                          uint16_t frames);
void control_music_output_abort_window(void);
uint8_t control_music_output_commit_window(void);
uint8_t control_music_output_finalize_window(void);
uint64_t control_music_output_first_unpublished_sample(uint64_t audio_sample);

/* CONTROL-owned lifecycle. START may atomically prepend the STOP of the
 * logical victim (including the global Multi pool victim). Calls made between
 * windows are staged and committed through the same dated buckets. AUDIO only
 * receives final, dated actions. */
uint8_t control_music_output_submit(const control_music_action_t *action,
                                    uint32_t causal_source_id,
                                    uint32_t generation);
uint8_t control_music_output_close_causal_sources(
    const uint32_t *causal_source_ids, uint16_t source_count,
    uint64_t due_sample);
void control_music_output_set_multi(brick_entity_id_t entity_id,
                                    uint8_t is_multi);
uint8_t control_music_output_count(brick_entity_id_t entity_id);
uint8_t control_music_output_panic_all(uint8_t send_transport_stop);
uint8_t control_music_output_has_alive(void);
uint8_t control_music_output_is_note_active_on_channel(
    uint8_t channel_zero_based, uint8_t note);

#endif /* CONTROL_MUSIC_OUTPUT_H */
