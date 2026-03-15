#ifndef DRV_HALL_H
#define DRV_HALL_H

#include <stdbool.h>
#include <stdint.h>

#include "brick_config.h"

typedef struct {
    /*
     * raw: last ADC sample before filtering (0..(1<<resolution)-1)
     * filtered: post-filter, post-calibration sample used for logic.
     */
    uint16_t raw;
    uint16_t filtered;
    uint16_t min;
    uint16_t max;
    uint16_t threshold;
    uint16_t hysteresis;

    uint32_t last_time;
    uint16_t last_raw;

    uint8_t  value;
    uint8_t  velocity;
    bool     pressed;
} hall_state_t;

void drv_hall_init(void);
bool drv_hall_is_initialized(void);
void drv_hall_task(void);

/*
 * Concurrency: drv_hall_task() is the single writer. The drv_hall_get_*()
 * accessors are lock-free readers and may be called from other threads.
 */
uint16_t drv_hall_get_raw(uint8_t i);
uint8_t  drv_hall_get_value(uint8_t i);
uint8_t  drv_hall_get_velocity(uint8_t i);
bool     drv_hall_is_pressed(uint8_t i);

#endif /* DRV_HALL_H */
