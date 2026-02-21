#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float   g;
    float   gi;
    float   state;
    uint8_t mode;
} fx_onepole_t;

void fx_onepole_init(fx_onepole_t *f);
void fx_onepole_set_freq(fx_onepole_t *f, float freq_norm);
void fx_onepole_set_mode(fx_onepole_t *f, uint8_t mode);
float fx_onepole_process(fx_onepole_t *f, float in);

#ifdef __cplusplus
}
#endif
