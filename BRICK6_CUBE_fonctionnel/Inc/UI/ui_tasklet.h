#ifndef UI_TASKLET_H
#define UI_TASKLET_H

#include <stdint.h>

void ui_tasklet_poll(void);
uint8_t ui_tasklet_is_initialized(void);

#endif /* UI_TASKLET_H */
