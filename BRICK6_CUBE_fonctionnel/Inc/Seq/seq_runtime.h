#ifndef SEQ_RUNTIME_H
#define SEQ_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint8_t running;
    seq_clock_src_t clock_src;
    uint8_t play_step[SEQ_TRACK_COUNT];
} seq_runtime_state_t;

void seq_runtime_init(void);
void seq_runtime_process(void);
const seq_runtime_state_t *seq_runtime_get_state(void);

#endif /* SEQ_RUNTIME_H */
