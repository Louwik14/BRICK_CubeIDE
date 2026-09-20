#ifndef SEQ_OCCUPANCY_DIAG_H
#define SEQ_OCCUPANCY_DIAG_H

#include <stdint.h>

#define SEQ_OCCUPANCY_DIAG_WORDS 144U
#define SEQ_OCCUPANCY_TRACE_ENTRIES 1024U
#define SEQ_OCCUPANCY_TRACE_WORDS (SEQ_OCCUPANCY_TRACE_ENTRIES * 4U)

extern volatile uint32_t g_seq_occupancy_diag[SEQ_OCCUPANCY_DIAG_WORDS];
extern volatile uint32_t g_seq_occupancy_trace[SEQ_OCCUPANCY_TRACE_WORDS];

void seq_occupancy_reset(void);
void seq_occupancy_block_start(uint32_t block_index, uint32_t anchor_cycle);
void seq_occupancy_block_end(void);
void seq_occupancy_burst_start(void);
void seq_occupancy_burst_end(void);
void seq_occupancy_preempt_enter(void);
void seq_occupancy_preempt_exit(void);

#endif
