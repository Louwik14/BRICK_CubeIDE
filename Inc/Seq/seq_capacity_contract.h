#ifndef SEQ_CAPACITY_CONTRACT_H
#define SEQ_CAPACITY_CONTRACT_H

#include "Seq/seq_model.h"
#include "Track/entity_types.h"

/* A GROUP master does not emit. Its eight mono children replace its eight
 * PLAY voices, so both legal topologies expose exactly 64 emitting voices. */
#define SEQ_PRODUCT_MAX_EMITTING_VOICES \
    (BRICK_ENTITY_TOP_LEVEL_COUNT * SEQ_PLAY_MAX_CAPACITY)
#define SEQ_PRODUCT_GROUP_MAX_EMITTING_VOICES \
    (((BRICK_ENTITY_TOP_LEVEL_COUNT - 1U) * SEQ_PLAY_MAX_CAPACITY) \
        + BRICK_ENTITY_GROUP_CHILD_COUNT)

/* At 300 BPM the shortest ROLL interval is 480 samples. A 64-frame horizon
 * can contain at most one point from each of two adjacent sources. Each On
 * may require one causal Off. */
#define SEQ_PRODUCT_MAX_SOURCE_GENERATIONS 3U
#define SEQ_PRODUCT_MAX_ACTIVE_SOURCES \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * SEQ_PRODUCT_MAX_SOURCE_GENERATIONS)
#define SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * 2U)
#define SEQ_PRODUCT_MAX_MUSIC_ACTIONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON * 2U)

_Static_assert(SEQ_PRODUCT_MAX_EMITTING_VOICES == 64U,
               "top-level sequencer voice proof changed");
_Static_assert(SEQ_PRODUCT_GROUP_MAX_EMITTING_VOICES
                   == SEQ_PRODUCT_MAX_EMITTING_VOICES,
               "GROUP sequencer voice proof changed");
_Static_assert(SEQ_PRODUCT_MAX_ACTIVE_SOURCES == 192U,
               "active scheduler source proof changed");
_Static_assert(SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON == 128U,
               "scheduler horizon On proof changed");
_Static_assert(SEQ_PRODUCT_MAX_MUSIC_ACTIONS_PER_HORIZON == 256U,
               "scheduler horizon action proof changed");

#endif
