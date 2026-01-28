#ifndef DIAGNOSTICS_TASKLET_H
#define DIAGNOSTICS_TASKLET_H

#include "stm32h7xx_hal.h"

void diagnostics_tasklet_poll(void);
void diagnostics_sdram_alloc_test(void);
void diagnostics_on_sd_stream_init(HAL_StatusTypeDef status);
void diagnostics_log(const char *message);
void diagnostics_logf(const char *fmt, ...);

#endif /* DIAGNOSTICS_TASKLET_H */
