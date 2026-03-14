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
    BTN_STOP,

    BTN_SHIFT,

    BTN_TRANSPOSE_UP,
    BTN_TRANSPOSE_DOWN,

    BTN_COPY,
    BTN_PASTE,
    BTN_SETTINGS,

    BTN_UNUSED_1,
    BTN_UNUSED_2,
    BTN_UNUSED_3,
    BTN_UNUSED_4,
    BTN_UNUSED_5,
    BTN_UNUSED_6,
    BTN_UNUSED_7,
    BTN_UNUSED_8,

    BTN_COUNT
} button_id_t;

_Static_assert((uint32_t)BTN_COUNT == 24U, "Button count must be 24");

#endif
