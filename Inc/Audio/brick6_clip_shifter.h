#ifndef BRICK6_CLIP_SHIFTER_H
#define BRICK6_CLIP_SHIFTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_CLIP_SHIFTER_DELAY_FRAMES 8192U
#define BRICK6_CLIP_SHIFTER_MIN_WINDOW_FRAMES 128U
#define BRICK6_CLIP_SHIFTER_MAX_WINDOW_FRAMES 8192U
#define BRICK6_CLIP_SHIFTER_MAX_HEADS 8U

typedef enum
{
    BRICK6_CLIP_SHIFTER_WINDOW_TRI = 0,
    BRICK6_CLIP_SHIFTER_WINDOW_HANN,
    BRICK6_CLIP_SHIFTER_WINDOW_SINE
} brick6_clip_shifter_window_t;

typedef struct
{
    float *buffer_l;
    float *buffer_r;
    float phase;
    float ratio;
    float window_frames;
    float head_previous_phase[BRICK6_CLIP_SHIFTER_MAX_HEADS];
    float head_dispersion_frames[BRICK6_CLIP_SHIFTER_MAX_HEADS];
    uint32_t random_state;
    uint16_t write_index;
    uint8_t heads;
    uint8_t window;
    uint8_t dispersion_percent;
} brick6_clip_shifter_t;

void brick6_clip_shifter_init(brick6_clip_shifter_t *shifter,
                              float *buffer_l,
                              float *buffer_r);
void brick6_clip_shifter_reset(brick6_clip_shifter_t *shifter);
void brick6_clip_shifter_set_window_frames(brick6_clip_shifter_t *shifter, uint16_t window_frames);
void brick6_clip_shifter_set_pitch_correction(brick6_clip_shifter_t *shifter, float pitch_correction);
void brick6_clip_shifter_set_experiment(brick6_clip_shifter_t *shifter,
                                        uint8_t heads,
                                        uint8_t window,
                                        uint8_t dispersion_percent);
void brick6_clip_shifter_process_mono(brick6_clip_shifter_t *shifter,
                                      float *mono,
                                      uint32_t frames);
void brick6_clip_shifter_process_stereo(brick6_clip_shifter_t *shifter,
                                        float *left,
                                        float *right,
                                        uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_CLIP_SHIFTER_H */
