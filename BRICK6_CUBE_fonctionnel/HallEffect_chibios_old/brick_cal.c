#include "brick_cal.h"

#include <limits.h>
#include <string.h>

#define BRICK_CAL_DEFAULT_DEADZONE_DEN 20U

struct brick_cal_model brick_cal_state = {0};

static bool channel_valid(struct brick_cal_pot* cal, uint8_t channel) {
  if (cal == NULL) {
    return false;
  }

  return channel < cal->length && channel < BRICK_NUM_HALL_SENSORS;
}

int brick_cal_pot_init(struct brick_cal_pot* cal, uint8_t resolution, uint8_t length) {
  if (cal == NULL) {
    return 1;
  }

  if (resolution > 16U) {
    resolution = 16U;
  }

  cal->resolution = resolution;
  cal->length = (length > BRICK_NUM_HALL_SENSORS) ? BRICK_NUM_HALL_SENSORS : length;
  cal->maximum = (uint16_t)((resolution >= 16U) ? UINT16_MAX : (1U << resolution));

  for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
    cal->min[i] = cal->maximum;
    cal->max[i] = 0;
    cal->enable[i] = 0;
    cal->detentlo[i] = 0;
    cal->detenthi[i] = cal->maximum;
  }

  return 0;
}

int brick_cal_pot_enable_range(struct brick_cal_pot* cal, uint8_t start, uint8_t length) {
  if (cal == NULL || cal->length == 0U) {
    return 1;
  }

  if (start >= cal->length) {
    return 1;
  }
  if ((uint16_t)start + length > cal->length) {
    return 1;
  }

  uint8_t end = (uint8_t)(start + length);
  for (uint8_t i = start; i < end; ++i) {
    cal->enable[i] = 1;
  }

  return 0;
}

int brick_cal_pot_enable_get(struct brick_cal_pot* cal, uint8_t channel, uint8_t* enable) {
  if (cal == NULL || enable == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  *enable = cal->enable[channel];
  return 0;
}

int brick_cal_pot_detent_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* detent, bool high) {
  if (cal == NULL || detent == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  *detent = high ? cal->detenthi[channel] : cal->detentlo[channel];
  return 0;
}

int brick_cal_pot_detent_set(struct brick_cal_pot* cal, uint8_t channel, uint16_t detent, bool high) {
  if (cal == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  if (!cal->enable[channel]) {
    return 1;
  }

  if (high) {
    cal->detenthi[channel] = detent;
  } else {
    cal->detentlo[channel] = detent;
  }

  return 0;
}

int brick_cal_pot_min_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* min_value) {
  if (cal == NULL || min_value == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  *min_value = cal->min[channel];
  return 0;
}

int brick_cal_pot_max_get(struct brick_cal_pot* cal, uint8_t channel, uint16_t* max_value) {
  if (cal == NULL || max_value == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  *max_value = cal->max[channel];
  return 0;
}

static void update_detent(struct brick_cal_pot* cal, uint8_t channel, uint16_t range, uint16_t center) {
  uint16_t half_deadzone = range / (BRICK_CAL_DEFAULT_DEADZONE_DEN * 2U);
  uint16_t lo = (center > half_deadzone) ? (uint16_t)(center - half_deadzone) : 0;
  uint16_t hi = center + half_deadzone;
  if (hi > cal->maximum) {
    hi = cal->maximum;
  }
  cal->detentlo[channel] = lo;
  cal->detenthi[channel] = hi;
}

static uint16_t clamp_to_maximum(struct brick_cal_pot* cal, uint16_t value) {
  if (cal == NULL) {
    return value;
  }
  if (value > cal->maximum) {
    return cal->maximum;
  }
  return value;
}

int brick_cal_pot_next(struct brick_cal_pot* cal, uint8_t channel, uint16_t in, uint16_t* out) {
  if (cal == NULL || out == NULL) {
    return 1;
  }

  if (!channel_valid(cal, channel)) {
    return 1;
  }

  in = clamp_to_maximum(cal, in);

  if (!cal->enable[channel]) {
    *out = in;
    return 0;
  }

  if (in < cal->min[channel]) {
    cal->min[channel] = in;
  }
  if (in > cal->max[channel]) {
    cal->max[channel] = in;
  }

  if (cal->max[channel] < cal->min[channel]) {
    cal->max[channel] = cal->min[channel];
  }

  uint16_t range = (uint16_t)(cal->max[channel] - cal->min[channel]);
  if (range == 0) {
    range = 1;
  }
  uint16_t center = (uint16_t)(cal->min[channel] + range / 2U);
  update_detent(cal, channel, range, center);

  *out = in;
  return 0;
}
