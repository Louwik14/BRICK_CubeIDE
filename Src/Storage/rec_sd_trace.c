#include "Storage/rec_sd_trace.h"

#include "Platform/memory_layout.h"
#include "stm32h7xx.h"

D2_IPC volatile rec_sd_trace_entry_t
    g_rec_sd_trace[REC_SD_TRACE_CAPACITY] __attribute__((used, externally_visible));
D2_IPC volatile uint32_t g_rec_sd_trace_next
    __attribute__((used, externally_visible));

void rec_sd_trace_log(rec_sd_trace_event_t event, uint32_t states,
                      uint32_t context, uint32_t detail, uint32_t frames,
                      uint32_t session, uint64_t sample)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t next = g_rec_sd_trace_next;
    volatile rec_sd_trace_entry_t *const entry =
        &g_rec_sd_trace[next % REC_SD_TRACE_CAPACITY];
    entry->sequence = 0U;
    entry->event = (uint32_t)event;
    entry->states = states;
    entry->context = context;
    entry->detail = detail;
    entry->frames = frames;
    entry->session = session;
    entry->sample_lo = (uint32_t)sample;
    __DMB();
    entry->sequence = next + 1U;
    g_rec_sd_trace_next = next + 1U;
    __set_PRIMASK(primask);
}
