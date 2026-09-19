#ifndef SEQ_CAPACITY_CONTRACT_H
#define SEQ_CAPACITY_CONTRACT_H

#include "Seq/seq_product_contract.h"

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

/* Harmonizer is the only multiplicative direct stage. Echo is addressed by
 * the canonical product lane (top track/lane, or GROUP child in the master's
 * eight-lane range) and Harmony branch. Top track 7 and its eight children
 * are mutually exclusive representations of the same product lanes. */
#define SEQ_PRODUCT_HARMONY_FANOUT_MAX 4U
#define SEQ_PRODUCT_ECHO_STATE_CAPACITY \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * SEQ_PRODUCT_HARMONY_FANOUT_MAX)
#define SEQ_PRODUCT_DIRECT_ONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON \
        * SEQ_PRODUCT_HARMONY_FANOUT_MAX)

/* Groove retains two adjacent due horizons per logical lane. Each resume is
 * one same-time batch containing every legal Harmony branch. */
#define SEQ_PRODUCT_GROOVE_RESUME_BATCHES_PER_LANE 2U
#define SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES \
        * SEQ_PRODUCT_GROOVE_RESUME_BATCHES_PER_LANE)
#define SEQ_PRODUCT_GROOVE_RESUME_EVENT_CAPACITY \
    (SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY \
        * SEQ_PRODUCT_HARMONY_FANOUT_MAX)

#define SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON 256U
#define SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON \
    (SEQ_PRODUCT_DIRECT_ONS_PER_HORIZON \
        + SEQ_PRODUCT_ECHO_STATE_CAPACITY \
        + SEQ_PRODUCT_GROOVE_RESUME_EVENT_CAPACITY)
#define SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON \
    (2U * SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON \
        + SEQ_PRODUCT_MAX_EMITTING_VOICES)
#define SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON \
    (SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON \
        + SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON)

#define SEQ_PRODUCT_SCHEDULER_ITEMS_MAX \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES \
        + SEQ_PRODUCT_MAX_EMITTING_VOICES \
        + SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY)

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
_Static_assert(SEQ_PRODUCT_ECHO_STATE_CAPACITY == 256U,
               "Echo branch identity proof changed");
_Static_assert(SEQ_PRODUCT_GROOVE_RESUME_BATCH_CAPACITY == 128U,
               "Groove resume batch proof changed");
_Static_assert(SEQ_PRODUCT_GROOVE_RESUME_EVENT_CAPACITY == 512U,
               "Groove resume event proof changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON == 1280U,
               "terminal On proof changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON == 2624U,
               "terminal note-event proof changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON == 2880U,
               "terminal publication proof changed");
_Static_assert(SEQ_PRODUCT_SCHEDULER_ITEMS_MAX == 256U,
               "scheduler item proof changed");

#endif
