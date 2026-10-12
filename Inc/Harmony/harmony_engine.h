#ifndef HARMONY_ENGINE_H
#define HARMONY_ENGINE_H

#include <stdint.h>
#include "Harmony/master_harmony.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HARMONY_MAX_ZONES 4U

typedef enum {
    HARMONY_MODE_OFF=0,
    HARMONY_MODE_CHORD1,
    HARMONY_MODE_CHORD2,
    HARMONY_MODE_BASS,
    HARMONY_MODE_DIATONIC,
    HARMONY_MODE_PARALLEL,
    HARMONY_MODE_COUNT
} harmony_mode_t;

typedef enum {
    HARMONY_SOURCE_NONE=0,
    HARMONY_SOURCE_CHORD,
    HARMONY_SOURCE_SCALE,
    HARMONY_SOURCE_ANCHOR
} harmony_source_kind_t;

typedef struct {
    uint8_t start_step;
    uint8_t end_step;
    uint8_t source_root;
    uint8_t source_chord;
    uint8_t scale_root;
    uint8_t scale_minor;
    uint8_t anchor_pc;
    uint8_t source_kind;
    uint8_t confidence;
} harmony_zone_t;

typedef struct {
    uint32_t revision;
    uint8_t zone_count;
    uint8_t valid;
    uint8_t reserved[2];
    harmony_zone_t zone[HARMONY_MAX_ZONES];
} harmony_track_plan_t;

void harmony_engine_init(void);
void harmony_engine_set_mode(uint8_t track, harmony_mode_t mode);
harmony_mode_t harmony_engine_get_mode(uint8_t track);
harmony_mode_t harmony_engine_resolve_mode(uint8_t track);
uint16_t harmony_engine_chord_mask(uint8_t chord);
uint8_t harmony_engine_transform(const harmony_track_plan_t *plan,
    harmony_mode_t requested_mode, uint8_t track, uint8_t original_step,
    uint8_t input_note,
    const master_harmony_state_t *target, uint8_t *out_note);
void harmony_engine_live_reset(void);

#ifdef __cplusplus
}
#endif
#endif
