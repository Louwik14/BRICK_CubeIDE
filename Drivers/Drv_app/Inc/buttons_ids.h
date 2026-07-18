#ifndef BUTTONS_IDS_H
#define BUTTONS_IDS_H

#include <stdint.h>

typedef enum
{
    BTN_PARAM_1 = 0,
    BTN_PARAM_2,
    BTN_PARAM_3,
    BTN_PARAM_4,
    BTN_PARAM_5,
    BTN_PARAM_6,
    BTN_PARAM_7,
    BTN_PARAM_8,

    BTN_PLAY,
    BTN_REC,

    BTN_SHIFT,

    BTN_TRANSPOSE_UP,
    BTN_TRANSPOSE_DOWN,

    BTN_COPY,
    BTN_PASTE,
    BTN_SETTINGS,

    BTN_PAGE_1,
    BTN_PAGE_2,
    BTN_PAGE_3,
    BTN_PAGE_4,
    BTN_ENCODER_1_PUSH,
    BTN_ENCODER_2_PUSH,
    BTN_ENCODER_3_PUSH,
    BTN_ENCODER_4_PUSH,
    BTN_STEP_1,
    BTN_STEP_2,
    BTN_STEP_3,
    BTN_STEP_4,
    BTN_STEP_5,
    BTN_STEP_6,
    BTN_STEP_7,
    BTN_STEP_8,
    BTN_STEP_9,
    BTN_STEP_10,
    BTN_STEP_11,
    BTN_STEP_12,
    BTN_STEP_13,
    BTN_STEP_14,
    BTN_STEP_15,
    BTN_STEP_16,
    BTN_UNUSED_5,
    BTN_UNUSED_6,
    BTN_UNUSED_7,
    BTN_UNUSED_8,

    BTN_COUNT
} button_id_t;

_Static_assert((uint32_t)BTN_COUNT == 44U, "Button count must be 44");

#endif
