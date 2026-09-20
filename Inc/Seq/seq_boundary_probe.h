#ifndef SEQ_BOUNDARY_PROBE_H
#define SEQ_BOUNDARY_PROBE_H

#include <stdint.h>
#include "stm32h7xx.h"

#define SEQ_FINE_DIAGNOSTICS 0

typedef enum {
    SEQ_PROBE_PREPARATION = 0,
    SEQ_PROBE_PLOCK,
    SEQ_PROBE_SOURCE,
    SEQ_PROBE_WALKER,
    SEQ_PROBE_ECHO,
    SEQ_PROBE_GENERATOR,
    SEQ_PROBE_DEFERRED,
    SEQ_PROBE_ADMISSION,
    SEQ_PROBE_TERMINAL,
    SEQ_PROBE_OTHER,
    SEQ_PROBE_PHASE_COUNT
} seq_probe_phase_t;

typedef enum {
    SEQ_PROBE_SOURCES_DUE = 0,
    SEQ_PROBE_NOTE_ON_CANDIDATES,
    SEQ_PROBE_NOTE_OFF_DUE,
    SEQ_PROBE_ECHO_DUE,
    SEQ_PROBE_ARP_EMISSIONS,
    SEQ_PROBE_EUCLID_EMISSIONS,
    SEQ_PROBE_DEFERRED_DUE,
    SEQ_PROBE_PARAM_TRANSITIONS,
    SEQ_PROBE_TERMINAL_APPENDS,
    SEQ_PROBE_WALKER_INVOCATIONS,
    SEQ_PROBE_WALKER_EVENTS_TRAVERSED,
    SEQ_PROBE_WALKER_EVENTS_PRODUCED,
    SEQ_PROBE_ACTIVITY_COUNT
} seq_probe_activity_t;

typedef struct {
    uint64_t phase_total[SEQ_PROBE_PHASE_COUNT];
    uint32_t phase_current[SEQ_PROBE_PHASE_COUNT];
    uint32_t phase_max[SEQ_PROBE_PHASE_COUNT];
    uint32_t phase_calls[SEQ_PROBE_PHASE_COUNT];
    uint32_t activity_total[SEQ_PROBE_ACTIVITY_COUNT];
    uint32_t activity_current[SEQ_PROBE_ACTIVITY_COUNT];
    uint32_t activity_max[SEQ_PROBE_ACTIVITY_COUNT];
    uint32_t boundaries;
    uint32_t active;
} seq_boundary_probe_state_t;

#if SEQ_FINE_DIAGNOSTICS
extern seq_boundary_probe_state_t g_seq_boundary_probe_state;
extern volatile uint32_t g_seq_boundary_diag[80];
#endif

static inline uint32_t seq_probe_begin(seq_probe_phase_t phase)
{
#if SEQ_FINE_DIAGNOSTICS
    (void)phase;
    return g_seq_boundary_probe_state.active ? DWT->CYCCNT : 0U;
#else
    (void)phase;return 0U;
#endif
}

static inline void seq_probe_end(seq_probe_phase_t phase, uint32_t started)
{
#if SEQ_FINE_DIAGNOSTICS
    if (g_seq_boundary_probe_state.active != 0U) {
        const uint32_t elapsed = DWT->CYCCNT - started;
        g_seq_boundary_probe_state.phase_total[phase] += elapsed;
        g_seq_boundary_probe_state.phase_current[phase] += elapsed;
        ++g_seq_boundary_probe_state.phase_calls[phase];
    }
#else
    (void)phase;(void)started;
#endif
}

static inline void seq_probe_activity(seq_probe_activity_t activity, uint32_t count)
{
#if SEQ_FINE_DIAGNOSTICS
    if (g_seq_boundary_probe_state.active != 0U) {
        g_seq_boundary_probe_state.activity_total[activity] += count;
        g_seq_boundary_probe_state.activity_current[activity] += count;
    }
#else
    (void)activity;(void)count;
#endif
}

#if SEQ_FINE_DIAGNOSTICS
void seq_boundary_probe_reset(void);
void seq_boundary_probe_block_begin(uint8_t boundary);
void seq_boundary_probe_block_end(void);
void seq_boundary_probe_publish(void);
#endif

#endif
