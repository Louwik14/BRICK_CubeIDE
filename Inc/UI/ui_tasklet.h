#ifndef UI_TASKLET_H
#define UI_TASKLET_H

#include <stdint.h>

void ui_tasklet_initialize(void);
void ui_tasklet_process_input(void);
void ui_tasklet_process_presentation(uint8_t deadline_due);
uint8_t ui_tasklet_is_initialized(void);

#endif /* UI_TASKLET_H */
