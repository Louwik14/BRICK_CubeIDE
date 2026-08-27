#ifndef CONTROL_MUSIC_OUTPUT_H
#define CONTROL_MUSIC_OUTPUT_H

#include <stdint.h>

#include "Audio/control_music_queue.h"

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
uint64_t control_music_output_first_unpublished_sample(uint64_t audio_sample);

/* CONTROL-owned lifecycle. START may atomically prepend the STOP of the
 * logical victim (including the global Multi pool victim). Calls made between
 * windows are staged and committed through the same dated buckets. AUDIO only
 * receives final, dated actions. */
uint8_t control_music_output_submit(const control_music_action_t *action);
uint8_t control_music_output_close_entity(brick_entity_id_t entity_id,
                                          uint64_t due_sample);
uint8_t control_music_output_close_entities(
    const brick_entity_id_t *entity_ids, uint8_t entity_count,
    uint64_t due_sample);
void control_music_output_panic_all(void);

#endif /* CONTROL_MUSIC_OUTPUT_H */
