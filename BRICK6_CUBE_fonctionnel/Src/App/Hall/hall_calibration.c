#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_filter.h"

#define CALIBRATION_HITS_REQUIRED 3U
#define CALIBRATION_PRESS_LEVEL   80U

static uint16_t cal_min[HALL_KEY_COUNT];
static uint16_t cal_max[HALL_KEY_COUNT];

static uint8_t cal_hits[HALL_KEY_COUNT];

static uint8_t calibration_done = 0U;

void hall_calibration_start(void)
{
    calibration_done = 0U;

    for(uint8_t i=0;i<HALL_KEY_COUNT;i++)
    {
        cal_min[i] = 0xFFFF;
        cal_max[i] = 0;
        cal_hits[i] = 0;
    }
}

void hall_calibration_process(void)
{
    if(calibration_done)
        return;

    uint8_t done_count = 0;

    for(uint8_t i=0;i<HALL_KEY_COUNT;i++)
    {
        uint16_t v = hall_filter_get(i);

        if(v < cal_min[i])
            cal_min[i] = v;

        if(v > cal_max[i])
            cal_max[i] = v;

        uint16_t range = cal_max[i] - cal_min[i];

        if(range > CALIBRATION_PRESS_LEVEL)
        {
            if(cal_hits[i] < CALIBRATION_HITS_REQUIRED)
                cal_hits[i]++;
        }

        if(cal_hits[i] >= CALIBRATION_HITS_REQUIRED)
            done_count++;
    }

    if(done_count == HALL_KEY_COUNT)
        calibration_done = 1U;
}

uint8_t hall_calibration_get_count(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    return cal_hits[key];
}

uint8_t hall_calibration_is_done(void)
{
    return calibration_done;
}

uint16_t hall_calibration_get_min(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    return cal_min[key];
}

uint16_t hall_calibration_get_max(uint8_t key)
{
    if(key >= HALL_KEY_COUNT)
        return 0;

    return cal_max[key];
}

void hall_calibration_save(void)
{
    /* à implémenter avec ton module flash */
}
