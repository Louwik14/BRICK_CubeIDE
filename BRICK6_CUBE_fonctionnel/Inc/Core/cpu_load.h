#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU load measurement from real IRQ occupancy.
 *
 * Model:
 *   cpu_permille = (elapsed_cycles / period_cycles) * 1000
 *
 * where:
 * - elapsed_cycles: cycles between irq_begin and irq_end for audio DMA IRQ
 * - period_cycles : cycles between two consecutive audio DMA IRQ entries
 */

void cpu_load_init(void);
void cpu_load_irq_begin(void);
void cpu_load_irq_end(void);

uint32_t cpu_load_get_permille(void);
uint32_t cpu_load_get_max(void);
uint32_t cpu_load_is_valid(void);

#ifdef __cplusplus
}
#endif
