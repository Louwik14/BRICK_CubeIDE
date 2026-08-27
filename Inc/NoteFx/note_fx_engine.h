#ifndef NOTE_FX_ENGINE_H
#define NOTE_FX_ENGINE_H

#include <stdint.h>
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_MAX_OUTPUTS 16U
#define NOTE_FX_EUCLID_MAX_SOURCES NOTE_FX_ARP_MAX_SOURCES
#define NOTE_FX_EUCLID_MAX_OWNED NOTE_FX_MAX_OUTPUTS
#define NOTE_FX_EUCLID_TOTAL_SOURCES \
    (NOTE_FX_TRACK_COUNT * NOTE_FX_SLOT_COUNT * NOTE_FX_EUCLID_MAX_SOURCES)
#define NOTE_FX_EUCLID_TOTAL_OWNED \
    (NOTE_FX_TRACK_COUNT * NOTE_FX_SLOT_COUNT * NOTE_FX_EUCLID_MAX_OWNED)

_Static_assert(NOTE_FX_EUCLID_MAX_SOURCES == 16U,
               "EUCLID source ledger capacity changed");
_Static_assert(NOTE_FX_EUCLID_MAX_OWNED >= NOTE_FX_EUCLID_MAX_SOURCES,
               "EUCLID owned capacity must cover source fan-out");

typedef note_fx_result_t (*note_fx_emit_fn)(const note_fx_event_t *, void *);

void note_fx_engine_init(void);
void note_fx_engine_configure(uint8_t track, uint8_t slot, uint8_t model, uint8_t rate, uint8_t style, uint8_t range);
note_fx_result_t note_fx_engine_source(const note_fx_event_t *event, note_fx_emit_fn emit, void *context);
note_fx_result_t note_fx_engine_stage_source(const note_fx_event_t *event, uint8_t slot,
                                             note_fx_emit_fn emit, void *context);
uint8_t note_fx_engine_is_generated_occurrence_current(
    uint8_t track, uint32_t occurrence_id, uint32_t generation);
note_fx_result_t note_fx_engine_process(uint64_t block_start, uint16_t frames,
                                        uint32_t samples_per_step_q16,
                                        note_fx_emit_fn emit, void *context);
/* Cleanup is transactional with respect to logical ownership: a refused STOP
 * is returned immediately and the corresponding owner remains live. */
note_fx_result_t note_fx_engine_cleanup(uint8_t track, uint64_t sample,
                                        note_fx_emit_fn emit, void *context);
void note_fx_engine_forget_output(uint8_t track, uint32_t output_id);

#endif
