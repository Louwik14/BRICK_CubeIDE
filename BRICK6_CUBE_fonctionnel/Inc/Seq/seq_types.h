#ifndef SEQ_TYPES_H
#define SEQ_TYPES_H

#include <stdint.h>

#define SEQ_TRACK_COUNT        8U
#define SEQ_STEPS_PER_PAGE    16U
#define SEQ_PAGE_COUNT         4U
#define SEQ_MAX_STEPS         (SEQ_STEPS_PER_PAGE * SEQ_PAGE_COUNT)

/* V1 fixed product limit: per-step lock cap. */
#define SEQ_STEP_MAX_LOCKS    16U

/* Memory capacity materialization (not a separate product rule). */
#define SEQ_PLOCK_POOL_CAP    (SEQ_TRACK_COUNT * SEQ_MAX_STEPS * SEQ_STEP_MAX_LOCKS)

typedef uint8_t seq_track_id_t;
typedef uint8_t seq_step_id_t;
typedef uint8_t seq_param8_t;
typedef uint16_t seq_value16_t;

typedef enum
{
    SEQ_CLOCK_SRC_INTERNAL = 0,
    SEQ_CLOCK_SRC_EXTERNAL_MIDI,
    SEQ_CLOCK_SRC_COUNT
} seq_clock_src_t;

#endif /* SEQ_TYPES_H */
