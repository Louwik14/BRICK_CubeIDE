#ifndef APP_HALL_HALL_CALIBRATION_H
#define APP_HALL_HALL_CALIBRATION_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

typedef struct
{
    uint16_t min[HALL_KEY_COUNT];
    uint16_t max[HALL_KEY_COUNT];
} hall_calibration_blob_t;

typedef enum
{
    HALL_USER_CAL_STAGE_IDLE = 0U,
    HALL_USER_CAL_STAGE_FORT,
    HALL_USER_CAL_STAGE_MID,
    HALL_USER_CAL_STAGE_SOFT,
    HALL_USER_CAL_STAGE_DONE
} hall_user_calibration_stage_t;

void hall_calibration_start(void);
void hall_calibration_process(void);

uint8_t hall_calibration_is_done(void);
uint8_t hall_calibration_is_key_done(uint8_t key);

uint8_t hall_calibration_get_count(uint8_t key);

uint16_t hall_calibration_get_min(uint8_t key);
uint16_t hall_calibration_get_max(uint8_t key);

uint8_t hall_calibration_load(void);
void hall_calibration_save(void);

void hall_user_calibration_start(void);
void hall_user_calibration_process(void);
uint8_t hall_user_calibration_is_done(void);
uint8_t hall_user_calibration_was_successful(void);
hall_user_calibration_stage_t hall_user_calibration_get_stage(void);
uint8_t hall_user_calibration_get_stage_count(void);
uint8_t hall_user_calibration_get_target_count(void);
uint8_t hall_user_velocity_profile_is_valid(void);
void hall_user_velocity_profile_get(hall_user_velocity_profile_t *profile);

#endif /* APP_HALL_HALL_CALIBRATION_H */
