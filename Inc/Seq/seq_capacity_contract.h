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

/* At 300 BPM the shortest ROLL interval is 600 samples. A 64-frame horizon
 * can contain at most one point from each of two adjacent sources. Each On
 * may require one causal Off. */
#define SEQ_PRODUCT_MAX_SOURCE_GENERATIONS 12U
#define SEQ_PRODUCT_MAX_ACTIVE_SOURCES \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * SEQ_PRODUCT_MAX_SOURCE_GENERATIONS)
#define SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * 2U)
#define SEQ_PRODUCT_MAX_MUSIC_ACTIONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON * 2U)

/* VOICER is the only multiplicative direct stage. Top track 7 and its
 * eight children are mutually exclusive representations of the same product
 * lanes. */
#define SEQ_PRODUCT_HARMONY_FANOUT_MAX 4U
#define SEQ_PRODUCT_HELD_STATE_CAPACITY \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES * SEQ_PRODUCT_HARMONY_FANOUT_MAX)
#define SEQ_PRODUCT_HELD_TOTAL_CAPACITY \
    (2U * SEQ_PRODUCT_HELD_STATE_CAPACITY)
#define SEQ_PRODUCT_DIRECT_ONS_PER_HORIZON \
    (SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON \
        * SEQ_PRODUCT_HARMONY_FANOUT_MAX)

/* Fully finalized NOTE_ON occurrences wait in one bounded calendar.  Active
 * note ownership remains the independent 64-entry ledger contract. */
#define SEQ_PRODUCT_FINAL_CALENDAR_CAPACITY 512U
#define SEQ_PRODUCT_ROLL_HITS_PER_STEP_MAX 4U
#define SEQ_PRODUCT_GROOVE_PENDING_ROLL_HITS_PER_LANE 32U
#define SEQ_PRODUCT_FINALIZED_OCCURRENCES_PER_LANE_MAX \
    (SEQ_PRODUCT_GROOVE_PENDING_ROLL_HITS_PER_LANE \
        * SEQ_PRODUCT_HARMONY_FANOUT_MAX)
#define SEQ_PRODUCT_FINALIZED_OCCURRENCES_MAX \
    (SEQ_PRODUCT_MAX_EMITTING_VOICES \
        * SEQ_PRODUCT_FINALIZED_OCCURRENCES_PER_LANE_MAX)
#define SEQ_PRODUCT_DEFERRED_CALENDAR_CAPACITY \
    (SEQ_PRODUCT_FINALIZED_OCCURRENCES_MAX \
        - SEQ_PRODUCT_FINAL_CALENDAR_CAPACITY)
#define SEQ_PRODUCT_DEFERRED_DESCRIPTOR_BYTES 32U
#define SEQ_PRODUCT_DEFERRED_CALENDAR_BUCKET_BYTES \
    (2U * 4096U * sizeof(uint16_t))
#define SEQ_PRODUCT_DEFERRED_CALENDAR_BYTES \
    (SEQ_PRODUCT_DEFERRED_CALENDAR_CAPACITY \
        * SEQ_PRODUCT_DEFERRED_DESCRIPTOR_BYTES \
        + SEQ_PRODUCT_DEFERRED_CALENDAR_BUCKET_BYTES)

#define SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON \
    (SEQ_LANE_CAPACITY * SEQ_STEP_MAX_LOCKS * 2U)
#define SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON \
    (SEQ_PRODUCT_DIRECT_ONS_PER_HORIZON \
        + SEQ_PRODUCT_FINAL_CALENDAR_CAPACITY)
#define SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON \
    (2U * SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON \
        + SEQ_PRODUCT_MAX_EMITTING_VOICES)
#define SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON \
    (SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON \
        + SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON)


_Static_assert(SEQ_PRODUCT_MAX_EMITTING_VOICES == 64U,
               "top-level sequencer voice proof changed");
_Static_assert(SEQ_PRODUCT_GROUP_MAX_EMITTING_VOICES
                   == SEQ_PRODUCT_MAX_EMITTING_VOICES,
               "GROUP sequencer voice proof changed");
_Static_assert(SEQ_PRODUCT_MAX_ACTIVE_SOURCES == 768U,
               "active source owner proof changed");
_Static_assert(SEQ_PRODUCT_MAX_NOTE_ONS_PER_HORIZON == 128U,
               "source horizon On proof changed");
_Static_assert(SEQ_PRODUCT_MAX_MUSIC_ACTIONS_PER_HORIZON == 256U,
               "source horizon action proof changed");
_Static_assert(SEQ_PRODUCT_HELD_STATE_CAPACITY == 256U,
               "held slot branch identity proof changed");
_Static_assert(SEQ_PRODUCT_HELD_TOTAL_CAPACITY == 512U,
               "held global identity proof changed");
_Static_assert(SEQ_PRODUCT_FINAL_CALENDAR_CAPACITY == 512U,
               "final calendar proof changed");
_Static_assert(SEQ_PRODUCT_ROLL_HITS_PER_STEP_MAX == 4U,
               "ROLL 1/64 hit count changed");
_Static_assert(SEQ_PRODUCT_FINALIZED_OCCURRENCES_PER_LANE_MAX == 128U,
               "per-lane finalized occurrence proof changed");
_Static_assert(SEQ_PRODUCT_FINALIZED_OCCURRENCES_MAX == 8192U,
               "ROLL 1/64 x VOICER finalized occurrence proof changed");
_Static_assert(SEQ_PRODUCT_DEFERRED_CALENDAR_CAPACITY == 7680U,
               "deferred occurrence partition proof changed");
_Static_assert(SEQ_PRODUCT_DEFERRED_CALENDAR_CAPACITY
                   + SEQ_PRODUCT_FINAL_CALENDAR_CAPACITY
               == SEQ_PRODUCT_FINALIZED_OCCURRENCES_MAX,
               "hierarchical calendar partition is incomplete");
_Static_assert(SEQ_PRODUCT_DEFERRED_CALENDAR_BYTES == 262144U,
               "deferred calendar SDRAM budget changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_ONS_PER_HORIZON == 1024U,
               "terminal On proof changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_NOTE_EVENTS_PER_HORIZON == 2112U,
               "terminal note-event proof changed");
_Static_assert(SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON == 1024U,
               "parameter transition proof changed");
_Static_assert(SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON == 3136U,
               "terminal publication proof changed");

#endif
