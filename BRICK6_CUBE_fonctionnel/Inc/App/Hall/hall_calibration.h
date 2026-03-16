#ifndef APP_HALL_HALL_CALIBRATION_H
#define APP_HALL_HALL_CALIBRATION_H

#include <stdint.h>

#define HALL_KEY_COUNT 16U

typedef struct
{
    uint16_t min[HALL_KEY_COUNT];
    uint16_t max[HALL_KEY_COUNT];
} hall_calibration_blob_t;

void hall_calibration_start(void);
void hall_calibration_process(void);

uint8_t hall_calibration_get_count(uint8_t key);
uint8_t hall_calibration_is_done(void);

uint16_t hall_calibration_get_min(uint8_t key);
uint16_t hall_calibration_get_max(uint8_t key);

uint8_t hall_calibration_load(void);
void hall_calibration_save(void);

#endif
