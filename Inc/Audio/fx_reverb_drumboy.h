#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fx_reverb_drumboy_t fx_reverb_drumboy_t;

#ifdef __cplusplus
struct fx_reverb_drumboy_t {
    uint8_t bypass;
    float sample_rate;

    float size;
    float decay;
    float predelay_s;
    float surround_s;
    float wet;

    float comb_feedback;
    float comb_decay1;
    float comb_decay2;

    uint32_t predelay_play;
    uint32_t predelay_write;
    uint32_t surround_play;
    uint32_t surround_write;

    static const uint32_t kPredelayBufferSize = 4500U;
    static const uint32_t kSurroundBufferSize = 900U;
    float predelay_buffer[kPredelayBufferSize];
    float surround_buffer[kSurroundBufferSize];

    static const uint32_t comb_size[8];
    uint32_t comb_index[8];
    float comb_filter[8];
    float comb_buffer0[1116];
    float comb_buffer1[1188];
    float comb_buffer2[1277];
    float comb_buffer3[1356];
    float comb_buffer4[1422];
    float comb_buffer5[1491];
    float comb_buffer6[1557];
    float comb_buffer7[1617];

    static const uint32_t apass_size[4];
    uint32_t apass_index[4];
    float apass_feedback;
    float apass_buffer0[225];
    float apass_buffer1[556];
    float apass_buffer2[441];
    float apass_buffer3[341];
};
#endif

void fx_reverb_drumboy_init(fx_reverb_drumboy_t *rev, float sample_rate);
void fx_reverb_drumboy_reset(fx_reverb_drumboy_t *rev);

void fx_reverb_drumboy_set_size(fx_reverb_drumboy_t *rev, float size_0_1);
void fx_reverb_drumboy_set_decay(fx_reverb_drumboy_t *rev, float decay_0_1);
void fx_reverb_drumboy_set_predelay(fx_reverb_drumboy_t *rev, float predelay_s);
void fx_reverb_drumboy_set_surround(fx_reverb_drumboy_t *rev, float surround_s);
void fx_reverb_drumboy_set_wet(fx_reverb_drumboy_t *rev, float wet_0_1);
void fx_reverb_drumboy_set_bypass(fx_reverb_drumboy_t *rev, uint8_t bypass);

void fx_reverb_drumboy_process_block(fx_reverb_drumboy_t *rev,
                                     const float *in_l,
                                     const float *in_r,
                                     float *out_l,
                                     float *out_r,
                                     uint32_t frames);

#ifdef __cplusplus
}
#endif
