#ifndef NOTE_FX_ENGINE_H
#define NOTE_FX_ENGINE_H

#include <stdint.h>
#include "NoteFx/note_fx_event.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_MAX_OUTPUTS 16U

typedef enum { NOTE_FX_EVENT_OFF = NOTE_EVENT_KIND_OFF, NOTE_FX_EVENT_ON = NOTE_EVENT_KIND_ON } note_fx_legacy_kind_t;
typedef note_fx_result_t (*note_fx_emit_fn)(const note_fx_event_t *, void *);
typedef struct { uint32_t saturations; uint32_t dropped_note_ons; } note_fx_diag_t;

void note_fx_engine_init(void);
void note_fx_engine_configure(uint8_t track, uint8_t slot, uint8_t model, uint8_t rate, uint8_t style, uint8_t range);
note_fx_result_t note_fx_engine_source(const note_fx_event_t *event, note_fx_emit_fn emit, void *context);
note_fx_result_t note_fx_engine_stage_source(const note_fx_event_t *event, uint8_t slot,
                                             note_fx_emit_fn emit, void *context);
void note_fx_engine_process(uint64_t block_start, uint16_t frames, uint32_t samples_per_step_q16, note_fx_emit_fn emit, void *context);
void note_fx_engine_cleanup(uint8_t track, uint64_t sample, note_fx_emit_fn emit, void *context);
note_fx_diag_t note_fx_engine_diag(uint8_t track);
uint64_t note_fx_engine_next_deadline(void);

#endif
