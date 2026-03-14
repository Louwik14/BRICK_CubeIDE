#ifndef LED_IDS_H
#define LED_IDS_H

#include "led_hw.h"

typedef enum
{
    LED_PARAM_1 = 0,
    LED_PARAM_2,
    LED_PARAM_3,
    LED_PARAM_4,
    LED_PARAM_5,
    LED_PARAM_6,
    LED_PARAM_7,
    LED_PARAM_8,

    LED_REC,

    LED_STEP_1,
    LED_STEP_2,
    LED_STEP_3,
    LED_STEP_4,
    LED_STEP_5,
    LED_STEP_6,
    LED_STEP_7,
    LED_STEP_8,
    LED_STEP_9,
    LED_STEP_10,
    LED_STEP_11,
    LED_STEP_12,
    LED_STEP_13,
    LED_STEP_14,
    LED_STEP_15,
    LED_STEP_16,

    LED_COUNT_TOTAL
} led_id_t;

_Static_assert((uint32_t)LED_COUNT_TOTAL == LED_HW_COUNT, "LED IDs must match LED hardware count");

#endif
