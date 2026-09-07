#ifndef UI_TASKLET_H
#define UI_TASKLET_H

#include <stdint.h>
#include "Storage/project_product.h"

void ui_tasklet_initialize(void);
void ui_tasklet_process_input(void);
void ui_tasklet_process_presentation(uint8_t deadline_due);
uint8_t ui_tasklet_is_initialized(void);
uint8_t ui_tasklet_project_presentation(project_product_command_t *command,
                                        project_product_progress_t *progress);

#endif /* UI_TASKLET_H */
