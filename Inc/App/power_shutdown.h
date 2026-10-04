#pragma once

#include <stdint.h>

typedef enum
{
    POWER_SHUTDOWN_IDLE = 0,
    POWER_SHUTDOWN_WAIT_STORAGE,
    POWER_SHUTDOWN_CAPTURE_PATTERN,
    POWER_SHUTDOWN_WRITE_RESUME,
    POWER_SHUTDOWN_FINAL,
    POWER_SHUTDOWN_FAILED
} power_shutdown_phase_t;

uint8_t power_shutdown_service(uint32_t now_ms);
power_shutdown_phase_t power_shutdown_phase(void);
uint8_t power_shutdown_mutations_frozen(void);
