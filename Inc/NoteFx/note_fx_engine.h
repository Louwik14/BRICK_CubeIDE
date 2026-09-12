#ifndef NOTE_FX_ENGINE_H
#define NOTE_FX_ENGINE_H

#include <stdint.h>
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_BATCH_CAPACITY 32U

typedef struct
{
    uint8_t instant_fanout;
    uint8_t temporal_fanout;
    uint8_t max_notes_per_group;
    uint8_t max_future_pending;
    uint8_t max_delay_divisions;
} note_fx_capacity_desc_t;

_Static_assert(NOTE_FX_HELD_PITCH_CAPACITY == 8U,
               "Note FX held-pitch contract changed");

typedef note_event_result_t (*note_fx_emit_fn)(const note_event_t *, void *);

void note_fx_engine_init(void);
void note_fx_engine_set_samples_per_step_q16(uint32_t samples_per_step_q16);
uint8_t note_fx_engine_capacity(uint8_t model,
                                note_fx_capacity_desc_t *out_capacity);
note_event_result_t note_fx_engine_configure(
    uint8_t track, uint8_t slot, uint8_t model, uint8_t rate,
    uint8_t style, uint8_t range);
note_event_result_t note_fx_engine_transform(
    uint8_t slot, const note_event_t *input, uint8_t input_count,
    note_event_t *output, uint8_t output_capacity, uint8_t *output_count);
note_event_result_t note_fx_engine_process(uint64_t block_start, uint16_t frames,
                                        uint32_t samples_per_step_q16,
                                        note_fx_emit_fn emit, void *context);
/* Cleanup is transactional with respect to logical ownership: a refused STOP
 * is returned immediately and the corresponding owner remains live. */
note_event_result_t note_fx_engine_cleanup(uint8_t track);
void note_fx_engine_forget_causal_source(uint8_t track,
                                         uint32_t causal_source_id);

#endif
