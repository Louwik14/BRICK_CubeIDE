#pragma once

#include "Param/param_ids.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float send_fx[2U];
    float bus_comp[8U];
    float saturation[4U];
    float reverb[7U];
    float delay[14U];
    float mod_fx[9U];
    float compressor[4U];
    float output[3U];
} param_global_control_state_t;

enum
{
    PARAM_GLOBAL_CONTROL_VALUE_COUNT = 51U
};

_Static_assert(sizeof(param_global_control_state_t)
                   == sizeof(float) * PARAM_GLOBAL_CONTROL_VALUE_COUNT,
               "global CONTROL layout cardinality changed");

/* Sparse CONTROL authority: only genuinely global product parameters are
 * accepted.  Entity, sequencer, keyboard and metronome values have their own
 * owners and are deliberately absent. */
void param_global_control_init(void);
uint8_t param_global_control_get(param_id_t id, float *out_value);
uint8_t param_global_control_set(param_id_t id, float value);
uint8_t param_global_control_capture(param_global_control_state_t *out_state);
uint8_t param_global_control_restore(const param_global_control_state_t *state);

#ifdef __cplusplus
}
#endif
