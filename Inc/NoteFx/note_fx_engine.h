#ifndef NOTE_FX_ENGINE_H
#define NOTE_FX_ENGINE_H

#include <stdint.h>
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_state.h"

#define NOTE_FX_MAX_OUTPUTS 16U
#define NOTE_FX_MAX_EMISSIONS_PER_BLOCK 8U

typedef enum { NOTE_FX_EVENT_OFF = 0, NOTE_FX_EVENT_ON = 1 } note_fx_event_type_t;
typedef struct { uint64_t sample; uint32_t token; uint32_t generation; uint8_t track, note, velocity, destination, type; } note_fx_event_t;
typedef void (*note_fx_emit_fn)(const note_fx_event_t *, void *);
typedef struct { uint32_t saturations; uint32_t dropped_note_ons; } note_fx_diag_t;

void note_fx_engine_init(void);
void note_fx_engine_configure(uint8_t track, uint8_t slot, uint8_t model, uint8_t rate, uint8_t style, uint8_t range);
uint8_t note_fx_engine_source(const note_fx_event_t *event, note_fx_emit_fn emit, void *context);
void note_fx_engine_process(uint64_t block_start, uint16_t frames, uint32_t samples_per_step_q16, note_fx_emit_fn emit, void *context);
void note_fx_engine_suspend(uint8_t track, uint8_t suspended, uint64_t sample, note_fx_emit_fn emit, void *context);
void note_fx_engine_cleanup(uint8_t track, uint64_t sample, note_fx_emit_fn emit, void *context);
note_fx_diag_t note_fx_engine_diag(uint8_t track);
uint64_t note_fx_engine_next_deadline(void);

#endif
