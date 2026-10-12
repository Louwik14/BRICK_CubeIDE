#ifndef HARMONY_ANALYSIS_H
#define HARMONY_ANALYSIS_H

#include <stdint.h>
#include "Harmony/harmony_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

void harmony_analysis_init(void);
void harmony_analysis_mark_dirty(uint8_t track);
void harmony_analysis_service(void);
uint8_t harmony_analysis_get_plan(uint8_t track,
    harmony_track_plan_t *out_plan);

#ifdef __cplusplus
}
#endif
#endif
