#ifndef SEQ_TYPES_H
#define SEQ_TYPES_H

#include <stdint.h>
#include "Core/track_topology.h"

#define SEQ_TRACK_COUNT        TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY
#define SEQ_STEPS_PER_PAGE    16U
#define SEQ_PAGE_COUNT         4U
#define SEQ_MAX_STEPS         (SEQ_STEPS_PER_PAGE * SEQ_PAGE_COUNT)
#define SEQ_DEFAULT_LENGTH_STEPS SEQ_STEPS_PER_PAGE

/* Play model: full note timeline and automation budget. */
#define SEQ_PLAY_STEP_MAX_LOCKS 32U
#define SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK 1024U

/* Special model: automation/actions only, no PLAY note payload. */
#define SEQ_SPECIAL_STEP_MAX_LOCKS 16U
#define SEQ_SPECIAL_PLOCK_POOL_CAP_PER_TRACK 512U

/* Maximum used only by Play-specific call sites and bounded stack buffers. */
#define SEQ_STEP_MAX_LOCKS SEQ_PLAY_STEP_MAX_LOCKS

/* Global per-track memory budget: shared by all steps of the track. */
#define SEQ_PLOCK_BUDGET_PER_TRACK SEQ_PLAY_PLOCK_POOL_CAP_PER_TRACK

typedef uint8_t seq_track_id_t;
typedef uint8_t seq_step_id_t;
typedef uint8_t seq_param_slot_t;
typedef uint16_t seq_value16_t;

typedef enum
{
    SEQ_CLOCK_SRC_INTERNAL = 0,
    SEQ_CLOCK_SRC_EXTERNAL_MIDI,
    SEQ_CLOCK_SRC_EXTERNAL_USB,
    SEQ_CLOCK_SRC_COUNT
} seq_clock_src_t;

#endif /* SEQ_TYPES_H */
