#ifndef NOTE_FX_WALKER_PROBE_H
#define NOTE_FX_WALKER_PROBE_H

#include <stdint.h>
#include "stm32h7xx.h"
#include "Seq/seq_boundary_probe.h"

typedef enum {
    NOTE_FX_WALKER_PROBABILITY = 0,
    NOTE_FX_WALKER_GATE,
    NOTE_FX_WALKER_GROOVE,
    NOTE_FX_WALKER_ECHO,
    NOTE_FX_WALKER_HARMONIZER,
    NOTE_FX_WALKER_CHORD,
    NOTE_FX_WALKER_ROLL,
    NOTE_FX_WALKER_ARP,
    NOTE_FX_WALKER_EUCLID,
    NOTE_FX_WALKER_COPY,
    NOTE_FX_WALKER_LOOKUP,
    NOTE_FX_WALKER_TERMINAL,
    NOTE_FX_WALKER_OTHER,
    NOTE_FX_WALKER_OFF,
    NOTE_FX_WALKER_CATEGORY_COUNT
} note_fx_walker_category_t;

typedef struct {
    uint64_t cycles;
    uint32_t max_cycles;
    uint32_t calls;
    uint32_t events_in;
    uint32_t events_out;
    uint32_t branches;
    uint32_t repeats;
    uint32_t held;
    uint32_t emissions;
} note_fx_walker_stat_t;

extern note_fx_walker_stat_t g_seq_walker_probe[NOTE_FX_WALKER_CATEGORY_COUNT];
extern volatile uint32_t g_seq_walker_diag[158];

static inline uint32_t note_fx_walker_probe_begin(void)
{return g_seq_boundary_probe_state.active ? DWT->CYCCNT : 0U;}

static inline void note_fx_walker_probe_record(note_fx_walker_category_t category,
    uint32_t started,uint32_t events_in,uint32_t events_out,uint32_t branches,
    uint32_t repeats,uint32_t held,uint32_t emissions)
{
    if(g_seq_boundary_probe_state.active!=0U){
        note_fx_walker_stat_t*s=&g_seq_walker_probe[category];
        const uint32_t elapsed=DWT->CYCCNT-started;
        s->cycles+=elapsed;if(elapsed>s->max_cycles)s->max_cycles=elapsed;
        ++s->calls;s->events_in+=events_in;s->events_out+=events_out;
        s->branches+=branches;s->repeats+=repeats;s->held+=held;
        s->emissions+=emissions;}
}

static inline uint64_t note_fx_walker_probe_cycles_total(void)
{uint64_t total=0U;
 for(uint8_t i=0U;i<NOTE_FX_WALKER_CATEGORY_COUNT;++i)
  total+=g_seq_walker_probe[i].cycles;
 return total;}

static inline void note_fx_walker_probe_record_elapsed(
    note_fx_walker_category_t category,uint32_t elapsed)
{if(g_seq_boundary_probe_state.active!=0U){note_fx_walker_stat_t*s=&g_seq_walker_probe[category];
 s->cycles+=elapsed;if(elapsed>s->max_cycles)s->max_cycles=elapsed;++s->calls;}}

void note_fx_walker_probe_reset(void);
void note_fx_walker_probe_publish(void);

#endif
