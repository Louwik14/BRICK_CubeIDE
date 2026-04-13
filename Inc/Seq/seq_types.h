#ifndef SEQ_TYPES_H
#define SEQ_TYPES_H

#include <stdint.h>

#define SEQ_TRACK_COUNT        14U
#define SEQ_STEPS_PER_PAGE    16U
#define SEQ_PAGE_COUNT         4U
#define SEQ_MAX_STEPS         (SEQ_STEPS_PER_PAGE * SEQ_PAGE_COUNT)

/* Local per-step product rule: one step can carry up to 32 p-locks. */
#define SEQ_STEP_MAX_LOCKS    32U

/* Global per-track memory budget: shared by all steps of the track. */
#define SEQ_PLOCK_BUDGET_PER_TRACK 1024U
#define SEQ_PLOCK_POOL_CAP_PER_TRACK SEQ_PLOCK_BUDGET_PER_TRACK
#define SEQ_PLOCK_POOL_CAP    (SEQ_TRACK_COUNT * SEQ_PLOCK_POOL_CAP_PER_TRACK)

typedef uint8_t seq_track_id_t;
typedef uint8_t seq_step_id_t;
typedef uint8_t seq_param8_t;
typedef uint16_t seq_value16_t;

typedef enum
{
    SEQ_CLOCK_SRC_INTERNAL = 0,
    SEQ_CLOCK_SRC_EXTERNAL_MIDI,
    SEQ_CLOCK_SRC_EXTERNAL_USB,
    SEQ_CLOCK_SRC_COUNT
} seq_clock_src_t;

#endif /* SEQ_TYPES_H */
