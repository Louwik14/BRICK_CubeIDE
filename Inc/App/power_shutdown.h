#pragma once

#include <stdint.h>

void power_shutdown_init(void);
void power_shutdown_sample(uint32_t now_ms);
uint8_t power_shutdown_process_deadline(uint32_t now_ms);
uint8_t power_shutdown_next_deadline(uint32_t now_ms,
                                     uint32_t *out_deadline_ms);
uint8_t power_shutdown_is_active(void);
