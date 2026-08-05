#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float current;
    float step;
    float end;
    uint32_t frames;
    uint8_t discontinuous;
} mod_destination_ramp_t;

void mod_destination_ramp_prepare(float start,
                                  float end,
                                  uint32_t frames,
                                  uint8_t discontinuous,
                                  mod_destination_ramp_t *out);
float mod_destination_ramp_value_at(const mod_destination_ramp_t *ramp, uint32_t index);

#ifdef __cplusplus
}
#endif
