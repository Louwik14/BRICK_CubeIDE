#ifndef UI_TASKLET_H
#define UI_TASKLET_H

#include <stdint.h>

typedef struct
{
    volatile uint32_t calls;
    volatile uint32_t last_cycles;
    volatile uint32_t max_cycles;
    volatile uint32_t lazy_init_count;
} ui_tasklet_metrics_t;

extern volatile ui_tasklet_metrics_t g_ui_tasklet_metrics;

void ui_tasklet_poll(void);
uint8_t ui_tasklet_is_initialized(void);

#endif /* UI_TASKLET_H */
