#ifndef AUDIO_XFADE_H
#define AUDIO_XFADE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_xfade_set(float xfade);
float audio_xfade_get(void);
float audio_xfade_smooth_next(float target, float *smoothed);
float audio_xfade_frame(float xfade_start,
                        float xfade_end,
                        uint32_t frame,
                        uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_XFADE_H */
