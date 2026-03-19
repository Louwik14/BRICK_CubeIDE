#ifndef DRV_HALL_H
#define DRV_HALL_H

#include <stdint.h>
#include <stdbool.h>

void hall_init(void);
void hall_update(void);
uint16_t hall_get(uint8_t index);
bool hall_get_note_on(uint8_t index);
bool hall_get_note_off(uint8_t index);
uint8_t hall_get_velocity(uint8_t index);
uint8_t hall_get_pressure(uint8_t index);
uint8_t hall_get_midi_value(uint8_t index);
uint16_t hall_get_raw(uint8_t index);

void hall_set_velocity_mode(uint8_t mode);
void hall_set_velocity_curve(uint8_t curve);

uint8_t hall_get_velocity_mode(void);
uint8_t hall_get_velocity_curve(void);

typedef enum {
  HALL_VEL_MODE_DV_PEAK = 0,
  HALL_VEL_MODE_TIME    = 1,
  HALL_VEL_MODE_ENERGY  = 2,

  HALL_VEL_MODE_COUNT
} hall_vel_mode_t;

typedef enum {
  HALL_VEL_CURVE_LINEAR = 0,
  HALL_VEL_CURVE_SOFT   = 1,
  HALL_VEL_CURVE_HARD   = 2,
  HALL_VEL_CURVE_LOG    = 3,
  HALL_VEL_CURVE_EXP    = 4,

  HALL_VEL_CURVE_COUNT
} hall_vel_curve_t;

/* -------- DEBUG ONLY -------- */
uint16_t hall_dbg_get_min(uint8_t i);
uint16_t hall_dbg_get_max(uint8_t i);
uint16_t hall_dbg_get_trig_lo(uint8_t i);
uint16_t hall_dbg_get_trig_hi(uint8_t i);
uint8_t  hall_dbg_get_state(uint8_t i);


#endif /* DRV_HALL_H */
