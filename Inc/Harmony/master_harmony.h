#ifndef MASTER_HARMONY_H
#define MASTER_HARMONY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MASTER_CHORD_MAJOR7 = 0,
    MASTER_CHORD_MAJOR,
    MASTER_CHORD_6,
    MASTER_CHORD_MINOR7_11,
    MASTER_CHORD_MAJOR7_9,
    MASTER_CHORD_ADD9,
    MASTER_CHORD_MINOR,
    MASTER_CHORD_MINOR6,
    MASTER_CHORD_MINOR7,
    MASTER_CHORD_MINOR7_FLAT5,
    MASTER_CHORD_MINOR_MAJOR7,
    MASTER_CHORD_MINOR7_9,
    MASTER_CHORD_MINOR_ADD9,
    MASTER_CHORD_7,
    MASTER_CHORD_7_SHARP5,
    MASTER_CHORD_7_FLAT9,
    MASTER_CHORD_7_9,
    MASTER_CHORD_7_SHARP9,
    MASTER_CHORD_7_SHARP11,
    MASTER_CHORD_7_FLAT13,
    MASTER_CHORD_7_13,
    MASTER_CHORD_7_SUS4,
    MASTER_CHORD_SUS4,
    MASTER_CHORD_DIM,
    MASTER_CHORD_AUG,
    MASTER_CHORD_6_9,
    MASTER_CHORD_7_FLAT5,
    MASTER_CHORD_TYPE_COUNT
} master_chord_type_t;

typedef struct {
    uint64_t effective_sample;
    uint32_t pattern_generation;
    uint8_t root;
    uint8_t type;
    uint8_t bass;
    uint8_t valid;
} master_harmony_state_t;

void master_harmony_reset(uint32_t pattern_generation, uint64_t sample);
uint8_t master_harmony_publish_notes(const uint8_t *notes, uint8_t note_count,
    uint32_t pattern_generation, uint64_t sample);
uint8_t master_harmony_get_current(master_harmony_state_t *out_state);
uint8_t master_harmony_resolve_at(uint64_t sample,
    master_harmony_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif
