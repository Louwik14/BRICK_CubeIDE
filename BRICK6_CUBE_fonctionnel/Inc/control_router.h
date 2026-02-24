#pragma once
#include <stdint.h>

typedef enum {
    CTRL_PARAM_GRAN_DENSITY = 0,
    CTRL_PARAM_GRAN_PITCH,
    CTRL_PARAM_GRAN_MIX,
    CTRL_PARAM_GRAN_FREEZE,
    CTRL_PARAM_GRAN_SPREAD,
    CTRL_PARAM_GRAN_STEREO,
} control_param_id_t;

void control_router_set_param(control_param_id_t id, float v);
