#pragma once

#include <stdint.h>

typedef struct
{
    uint32_t last_permille;
    uint32_t avg_permille;
    uint32_t peak_permille;
    uint32_t peak_recent_permille;
    uint32_t over_80_count;
    uint32_t over_90_count;
    uint32_t over_100_count;
    uint32_t block_count;
    uint32_t counter_valid;
} cpu_load_metrics_t;

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
uint32_t cpu_load_get_avg_permille(void);
uint32_t cpu_load_get_peak_recent_permille(void);
uint32_t cpu_load_get_over_80_count(void);
uint32_t cpu_load_get_over_90_count(void);
uint32_t cpu_load_get_over_100_count(void);
uint32_t cpu_load_get_block_count(void);
uint32_t cpu_load_is_valid(void);

void cpu_load_get_metrics(cpu_load_metrics_t *metrics);
void cpu_load_reset_peak(void);
void cpu_load_reset_measurement(void);

#ifdef __cplusplus
}
#endif
