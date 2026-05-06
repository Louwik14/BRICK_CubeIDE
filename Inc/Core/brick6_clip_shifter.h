#ifndef BRICK6_CLIP_SHIFTER_H
#define BRICK6_CLIP_SHIFTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_CLIP_SHIFTER_DELAY_FRAMES 2048U
#define BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES 128U
#define BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES 2047U

typedef struct
{
    float buffer_l[BRICK6_CLIP_SHIFTER_DELAY_FRAMES];
    float buffer_r[BRICK6_CLIP_SHIFTER_DELAY_FRAMES];
    float phase;
    float ratio;
    float window_frames;
    uint16_t write_index;
} brick6_clip_shifter_t;

void brick6_clip_shifter_init(brick6_clip_shifter_t *shifter);
void brick6_clip_shifter_reset(brick6_clip_shifter_t *shifter);
void brick6_clip_shifter_set_window_frames(brick6_clip_shifter_t *shifter, uint16_t window_frames);
void brick6_clip_shifter_set_pitch_correction(brick6_clip_shifter_t *shifter, float pitch_correction);
void brick6_clip_shifter_process_stereo(brick6_clip_shifter_t *shifter,
                                        float *left,
                                        float *right,
                                        uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_CLIP_SHIFTER_H */
