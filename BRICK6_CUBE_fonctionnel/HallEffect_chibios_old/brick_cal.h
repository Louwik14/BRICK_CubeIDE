#ifndef BRICK_CAL_H
#define BRICK_CAL_H

#include <stdbool.h>
#include <stdint.h>

#include "brick_config.h"

/*
 * The calibration helpers operate on caller-provided storage so multiple
 * instances can coexist (e.g., stack or static). The optional
 * brick_cal_state below is a convenience global for users that do not want
 * to manage their own struct lifetime.
 */
struct brick_cal_pot {
  uint8_t resolution;
  uint8_t length;
  uint16_t maximum;
  uint16_t min[BRICK_NUM_HALL_SENSORS];
  uint16_t max[BRICK_NUM_HALL_SENSORS];
  uint16_t detentlo[BRICK_NUM_HALL_SENSORS];
  uint16_t detenthi[BRICK_NUM_HALL_SENSORS];
  uint8_t enable[BRICK_NUM_HALL_SENSORS];
};

int brick_cal_pot_init(struct brick_cal_pot* cal, uint8_t resolution, uint8_t length);
int brick_cal_pot_enable_range(struct brick_cal_pot* cal, uint8_t start, uint8_t length);
int brick_cal_pot_enable_get(struct brick_cal_pot* cal, uint8_t channel, uint8_t* enable);
int brick_cal_pot_detent_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* detent, bool high);
int brick_cal_pot_detent_set(struct brick_cal_pot* cal, uint8_t channel, uint16_t detent, bool high);
int brick_cal_pot_min_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* min_value);
int brick_cal_pot_max_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* max_value);
int brick_cal_pot_next(struct brick_cal_pot* cal, uint8_t channel, uint16_t in, uint16_t* out);

struct brick_cal_model {
  struct brick_cal_pot potmeter;
};

extern struct brick_cal_model brick_cal_state;

#endif /* BRICK_CAL_H */
