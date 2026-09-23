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

void note_fx_engine_init(void);
void note_fx_engine_set_samples_per_step_q16(uint32_t samples_per_step_q16);
void note_fx_engine_set_time_reference(uint64_t sample_time,
                                       uint64_t transport_position_q16,
                                       uint32_t samples_per_step_q16);
note_event_result_t note_fx_engine_configure(
    uint8_t track, uint8_t slot, uint8_t model, uint8_t rate,
    uint8_t style, uint8_t range, uint8_t param4);
note_event_result_t note_fx_engine_transform(
    uint8_t slot, uint8_t position, const note_event_t *input, uint8_t input_count,
    note_event_t *output, uint8_t output_capacity, uint8_t *output_count);
note_event_result_t note_fx_engine_transform_prepared(
    uint8_t slot, uint8_t position, const note_event_t *input, uint8_t input_count,
    note_event_t *output, uint8_t output_capacity, uint8_t *output_count);
uint8_t note_fx_engine_slot_at(uint8_t track, uint8_t order, uint8_t position);
uint8_t note_fx_engine_suffix_is_temporal(uint8_t track, uint8_t order,
                                          uint8_t stage);
note_event_result_t note_fx_engine_process(
    uint8_t track, uint64_t block_start, uint32_t horizon_samples,
    uint32_t samples_per_step_q16,
    uint64_t transport_position_q16,
    const uint32_t pattern_position_q16[NOTE_FX_TRACK_COUNT],
    const uint8_t pattern_length[NOTE_FX_TRACK_COUNT],
    uint8_t scale_index, uint8_t root_index,
    note_fx_emit_fn emit, void *context);
/* Cleanup is transactional with respect to logical ownership: a refused STOP
 * is returned immediately and the corresponding owner remains live. */
note_event_result_t note_fx_engine_cleanup(uint8_t track);

void note_fx_engine_forget_causal_source(uint8_t track,
                                         uint32_t causal_source_id);
void note_fx_engine_release_terminal(const note_event_t *event);

#endif
