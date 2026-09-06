#pragma once

#include <stdint.h>

void board_power_delay_ms(uint32_t ms);
void board_power_hold_enable_after_boot_press(void);
void board_power_shutdown_init(void);
void board_power_shutdown_sample(uint32_t now_ms);
void board_power_shutdown_poll_irq(void);
uint8_t board_power_shutdown_process_deadline(uint32_t now_ms);
uint8_t board_power_shutdown_next_deadline(uint32_t now_ms,
                                           uint32_t *out_deadline_ms);
void board_power_usb_host_off(void);
void board_power_shutdown_cut(void);
