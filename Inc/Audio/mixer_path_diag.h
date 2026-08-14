#ifndef BRICK6_MIXER_PATH_DIAG_H
#define BRICK6_MIXER_PATH_DIAG_H

#include <stdint.h>

#define MIXER_PATH_DIAG_CAPACITY 16U
#define MIXER_PATH_DIAG_MAX_BLOCKS 16U
#define MIXER_PATH_DIAG_NO_SLOT 0xFFU

typedef struct
{
    uint32_t seq;
    uint32_t block_seq;
    uint8_t entity_id;
    uint8_t mix_track_id;
    uint8_t source_flags;
    uint8_t track_flags;
    uint16_t frames;
    uint8_t invalid_flags;
    uint8_t stage_flags;
    float peak_a_l;
    float peak_a_r;
    float peak_b_l;
    float peak_b_r;
    float peak_c_l;
    float peak_c_r;
    float peak_d_l;
    float peak_d_r;
    float gain_first;
    float gain_last;
    float pan_l_first;
    float pan_l_last;
    float pan_r_first;
    float pan_r_last;
    float vca_first;
    float vca_last;
    float mute_gain_first;
    float mute_gain_last;
} mixer_path_diag_snapshot_t;

_Static_assert(sizeof(mixer_path_diag_snapshot_t) == 88U,
               "mixer path diagnostic snapshot must remain 88 bytes");

#if defined(BRICK6_MIXER_PATH_DIAG)

extern volatile mixer_path_diag_snapshot_t
    mixer_path_diag_snapshots[MIXER_PATH_DIAG_CAPACITY];
extern volatile uint32_t mixer_path_diag_write_seq;
extern volatile uint32_t mixer_path_diag_block_seq;
extern volatile uint32_t mixer_path_diag_captured_blocks;
extern volatile uint8_t mixer_path_diag_enabled;
extern volatile uint8_t mixer_path_diag_remaining_blocks;
extern volatile uint8_t mixer_path_diag_entity_id;

void mixer_path_diag_reset(void);
void mixer_path_diag_block_begin(uint32_t frames);
uint8_t mixer_path_diag_begin(uint8_t entity_id, uint8_t mix_track_id,
                              uint8_t source_flags, uint8_t track_flags,
                              const float *left, const float *right,
                              uint32_t frames);
void mixer_path_diag_set_coefficients(uint8_t index,
                                      float gain_first, float gain_last,
                                      float pan_l_first, float pan_l_last,
                                      float pan_r_first, float pan_r_last,
                                      float vca_first, float vca_last,
                                      float mute_first, float mute_last);
void mixer_path_diag_capture_bc_sample(uint8_t index,
                                       float b_l, float b_r,
                                       float c_l, float c_r);
void mixer_path_diag_capture_bc(uint8_t index,
                                const float *left, const float *right,
                                uint32_t frames, float contribution_gain);
void mixer_path_diag_capture_d(uint8_t index,
                               const float *left, const float *right,
                               uint32_t frames);
void mixer_path_diag_commit(uint8_t index);

#define MIXER_PATH_DIAG_CAPTURE_BC_SAMPLE(index_, b_l_, b_r_, c_l_, c_r_) \
    do { \
        if ((index_) != MIXER_PATH_DIAG_NO_SLOT) \
            mixer_path_diag_capture_bc_sample((index_), (b_l_), (b_r_), \
                                              (c_l_), (c_r_)); \
    } while (0)

#else

#define MIXER_PATH_DIAG_CAPTURE_BC_SAMPLE(index_, b_l_, b_r_, c_l_, c_r_) \
    ((void)0)

#endif

#endif /* BRICK6_MIXER_PATH_DIAG_H */
