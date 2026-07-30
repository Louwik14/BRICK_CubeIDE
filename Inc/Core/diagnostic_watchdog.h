#ifndef DIAGNOSTIC_WATCHDOG_H
#define DIAGNOSTIC_WATCHDOG_H

#include <stdint.h>

void diagnostic_watchdog_init(void);
uint8_t diagnostic_watchdog_arm(uint32_t engine_tick);
void diagnostic_watchdog_main_loop_heartbeat(uint32_t engine_tick);

#endif
