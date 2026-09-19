#ifndef NOTE_FX_ENGINE_H
#define NOTE_FX_ENGINE_H

#include <stdint.h>
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_capacity_contract.h"

#define NOTE_FX_BATCH_CAPACITY 32U
#define NOTE_FX_HELD_STATE_CAPACITY SEQ_PRODUCT_HELD_STATE_CAPACITY

_Static_assert(NOTE_FX_HELD_STATE_CAPACITY == 256U,
               "Note FX canonical held-state contract changed");

typedef note_event_result_t (*note_fx_emit_fn)(const note_event_t *, void *);

typedef struct
{
    uint32_t alloc_attempts;
    uint32_t alloc_failures;
    uint32_t reuse_hits;
    uint32_t free_hits;
    uint32_t key_collision_count;
    uint16_t active;
    uint16_t active_peak;
} note_fx_echo_diag_t;

void note_fx_engine_init(void);
void note_fx_engine_echo_diag_capture(note_fx_echo_diag_t *out);
void note_fx_engine_set_samples_per_step_q16(uint32_t samples_per_step_q16);
note_event_result_t note_fx_engine_configure(
    uint8_t track, uint8_t slot, uint8_t model, uint8_t rate,
    uint8_t style, uint8_t range, uint16_t dependency_versions);
note_event_result_t note_fx_engine_transform(
    uint8_t slot, const note_event_t *input, uint8_t input_count,
    note_event_t *output, uint8_t output_capacity, uint8_t *output_count);
note_event_result_t note_fx_engine_process(
    uint64_t block_start, uint16_t frames, uint32_t samples_per_step_q16,
    uint64_t transport_position_q16,
    const uint32_t pattern_position_q16[NOTE_FX_TRACK_COUNT],
    uint8_t scale_index, uint8_t root_index,
    note_fx_emit_fn emit, void *context);
/* Cleanup is transactional with respect to logical ownership: a refused STOP
 * is returned immediately and the corresponding owner remains live. */
note_event_result_t note_fx_engine_cleanup(uint8_t track);

void note_fx_engine_forget_causal_source(uint8_t track,
                                         uint32_t causal_source_id);
void note_fx_engine_release_terminal(const note_event_t *event);
void note_fx_engine_forget_dependency(uint8_t track, uint8_t owner_slot);
void note_fx_engine_forget_causal_sources_from_slot(
    uint8_t track, uint8_t first_slot, const uint32_t *source_ids,
    uint16_t source_count);

#endif
