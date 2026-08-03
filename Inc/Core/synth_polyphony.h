#pragma once

#include <stdint.h>

#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_POLYPHONY_MAX_VOICES 8U
#define SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET 16U
#define SYNTH_POLYPHONY_NO_VOICE   0xFFU
#define SYNTH_POLYPHONY_TRACK_CAPACITY 8U
#define SYNTH_POLYPHONY_INSTANCE(track, voice) synth_polyphony_get_slot((track), (voice))

typedef enum
{
    SYNTH_POLY_VOICE_FREE = 0,
    SYNTH_POLY_VOICE_HELD,
    SYNTH_POLY_VOICE_RELEASE
} synth_poly_voice_state_t;

typedef enum
{
    SYNTH_POLY_SOURCE_MANUAL = 0,
    SYNTH_POLY_SOURCE_SEQUENCER
} synth_poly_source_t;

typedef struct
{
    uint8_t voice;
    uint8_t note;
} synth_poly_release_t;

void synth_polyphony_init(void);
uint8_t synth_polyphony_note_on(uint8_t track, uint8_t note);
uint8_t synth_polyphony_note_off(uint8_t track, uint8_t note);
uint8_t synth_polyphony_note_on_from(uint8_t track, uint8_t note, synth_poly_source_t source);
uint8_t synth_polyphony_note_off_from(uint8_t track, uint8_t note, synth_poly_source_t source);
uint8_t synth_polyphony_note_on_occurrence_from(uint8_t track, uint8_t note,
                                                synth_poly_source_t source,
                                                uint32_t occurrence_id);
uint8_t synth_polyphony_note_off_occurrence_from(uint8_t track,
                                                 synth_poly_source_t source,
                                                 uint32_t occurrence_id);
uint8_t synth_polyphony_occurrence_is_active(uint8_t track,
                                             synth_poly_source_t source,
                                             uint32_t occurrence_id);
uint8_t synth_polyphony_release_source(uint8_t track,
                                      synth_poly_source_t source,
                                      synth_poly_release_t *out,
                                      uint8_t capacity);
uint8_t synth_polyphony_release_all(uint8_t track,
                                   synth_poly_release_t *out,
                                   uint8_t capacity);
void synth_polyphony_all_notes_off(uint8_t track);
uint8_t synth_polyphony_set_voice_count(uint8_t track, uint8_t count);
uint8_t synth_polyphony_set_track_active(uint8_t track, uint8_t active, uint8_t engine);
uint8_t synth_polyphony_get_track_active(uint8_t track);
uint8_t synth_polyphony_get_slot(uint8_t track, uint8_t voice);
uint8_t synth_polyphony_get_available_for_track(uint8_t track);
uint8_t synth_polyphony_get_free_count(void);
uint8_t synth_polyphony_validate_ownership(void);
void synth_polyphony_reset_track(uint8_t track);
void synth_polyphony_panic(void);
uint8_t synth_polyphony_get_voice_count(uint8_t track);
uint8_t synth_polyphony_get_render_voice_count(uint8_t track);
uint8_t synth_polyphony_get_renderable_voice_mask(uint8_t track);
void synth_polyphony_set_spread(uint8_t track, float spread);
float synth_polyphony_get_spread(uint8_t track);
float synth_polyphony_get_voice_pan(uint8_t track, uint8_t voice);
uint8_t synth_polyphony_voice_is_renderable(uint8_t track, uint8_t voice);
void synth_polyphony_voice_release_complete(uint8_t track, uint8_t voice);

#ifdef __cplusplus
}
#endif
