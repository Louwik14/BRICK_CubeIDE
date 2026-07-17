#ifndef BRICK_HALL_SURFACE_H
#define BRICK_HALL_SURFACE_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

void hall_surface_refresh(void);
uint8_t hall_surface_is_pressed(uint8_t lane);
uint16_t hall_surface_pressure_u16(uint8_t lane);
uint8_t hall_surface_is_binary(void);

#ifdef __cplusplus
}
#endif

#endif
