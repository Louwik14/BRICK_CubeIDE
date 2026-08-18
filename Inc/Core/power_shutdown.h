#pragma once

#include <stdint.h>

/* Services the non-blocking Power-button shutdown state machine.
 * Returns non-zero while normal application processing must be suspended. */
uint8_t power_shutdown_service(uint32_t now_ms);
