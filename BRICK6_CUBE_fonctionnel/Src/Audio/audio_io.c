/**
 * @file audio_io.c
 * @brief Conversion rapide TDM int24 <-> buffers float tracks stéréo.
 *
 * Rôle du module:
 * - Dépaqueter les slots TDM RX de SAI1/SAI2 en buffers float par track active.
 * - Repaqueter les tracks float vers TX TDM4 de SAI1/SAI2.
 */

#include "audio_io.h"

#include <string.h>
#include <arm_acle.h>
#include "stm32h743xx.h"

#define AUDIO_TDM_SLOTS 4U

static inline int32_t s24_sign_extend(int32_t x)
{
    return (x << 8) >> 8;
}

static inline float s242f_fast(int32_t x, float gain)
{
    return (float)s24_sign_extend(x) * gain;
}

static inline int32_t f2s24_fast(float x)
{
    const float clamped = __builtin_fmaxf(-1.0f, __builtin_fminf(x, 0.9999998807907104f));
    const int32_t q = (int32_t)(clamped * 8388607.0f);
    return q & 0x00FFFFFF;
}

static inline int32_t f2s24_fast_ssat(float x)
{
    const int32_t q = (int32_t)(x * 8388608.0f);
    const int32_t sat = __SSAT(q, 24);
    return sat & 0x00FFFFFF;
}

void audio_io_unpack(const int32_t *AUDIO_RESTRICT rx_sai1,
                     const int32_t *AUDIO_RESTRICT rx_sai2,
                     StereoTrack *AUDIO_RESTRICT track_buf,
                     uint32_t frames,
                     float in_scale)
{
    const uint32_t tr0_on = (uint32_t)track_buf[0].enabled;
    const uint32_t tr1_on = (uint32_t)track_buf[1].enabled;
    const uint32_t tr2_on = (uint32_t)track_buf[2].enabled;
    const uint32_t tr3_on = (uint32_t)track_buf[3].enabled;

    float *AUDIO_RESTRICT tr0_l = track_buf[0].L;
    float *AUDIO_RESTRICT tr0_r = track_buf[0].R;
    float *AUDIO_RESTRICT tr1_l = track_buf[1].L;
    float *AUDIO_RESTRICT tr1_r = track_buf[1].R;
    float *AUDIO_RESTRICT tr2_l = track_buf[2].L;
    float *AUDIO_RESTRICT tr2_r = track_buf[2].R;
    float *AUDIO_RESTRICT tr3_l = track_buf[3].L;
    float *AUDIO_RESTRICT tr3_r = track_buf[3].R;

    if(tr0_on == 0U) { memset(tr0_l, 0, frames * sizeof(float)); memset(tr0_r, 0, frames * sizeof(float)); }
    if(tr1_on == 0U) { memset(tr1_l, 0, frames * sizeof(float)); memset(tr1_r, 0, frames * sizeof(float)); }
    if(tr2_on == 0U) { memset(tr2_l, 0, frames * sizeof(float)); memset(tr2_r, 0, frames * sizeof(float)); }
    if(tr3_on == 0U) { memset(tr3_l, 0, frames * sizeof(float)); memset(tr3_r, 0, frames * sizeof(float)); }

    const int32_t *AUDIO_RESTRICT prx1 = rx_sai1;
    const int32_t *AUDIO_RESTRICT prx2 = rx_sai2;

    for(uint32_t n = 0; n < frames; n++)
    {
        if(tr0_on)
        {
            tr0_l[n] = s242f_fast(prx1[0], in_scale);
            tr0_r[n] = s242f_fast(prx1[1], in_scale);
        }
        if(tr1_on)
        {
            tr1_l[n] = s242f_fast(prx1[2], in_scale);
            tr1_r[n] = s242f_fast(prx1[3], in_scale);
        }
        if(tr2_on)
        {
            tr2_l[n] = s242f_fast(prx2[0], in_scale);
            tr2_r[n] = s242f_fast(prx2[1], in_scale);
        }
        if(tr3_on)
        {
            tr3_l[n] = s242f_fast(prx2[2], in_scale);
            tr3_r[n] = s242f_fast(prx2[3], in_scale);
        }

        prx1 += AUDIO_TDM_SLOTS;
        prx2 += AUDIO_TDM_SLOTS;
    }
}

void audio_io_pack(int32_t *AUDIO_RESTRICT tx_sai1,
                   int32_t *AUDIO_RESTRICT tx_sai2,
                   const StereoTrack *AUDIO_RESTRICT tracks,
                   uint32_t frames,
                   float out_gain)
{
    int32_t *AUDIO_RESTRICT ptx1 = tx_sai1;
    int32_t *AUDIO_RESTRICT ptx2 = tx_sai2;

    for(uint32_t n = 0; n < frames; n++)
    {
        const float t1_l = tracks[0].L[n] * out_gain;
        const float t1_r = tracks[0].R[n] * out_gain;
        const float t2_l = tracks[1].L[n] * out_gain;
        const float t2_r = tracks[1].R[n] * out_gain;
        const float t3_l = tracks[2].L[n] * out_gain;
        const float t3_r = tracks[2].R[n] * out_gain;
        const float t4_l = tracks[3].L[n] * out_gain;
        const float t4_r = tracks[3].R[n] * out_gain;

#if defined(USE_F2S24_SSAT)
        ptx1[0] = f2s24_fast_ssat(t1_l);
        ptx1[1] = f2s24_fast_ssat(t1_r);
        ptx1[2] = f2s24_fast_ssat(t2_l);
        ptx1[3] = f2s24_fast_ssat(t2_r);

        ptx2[0] = f2s24_fast_ssat(t3_l);
        ptx2[1] = f2s24_fast_ssat(t3_r);
        ptx2[2] = f2s24_fast_ssat(t4_l);
        ptx2[3] = f2s24_fast_ssat(t4_r);
#else
        ptx1[0] = f2s24_fast(t1_l);
        ptx1[1] = f2s24_fast(t1_r);
        ptx1[2] = f2s24_fast(t2_l);
        ptx1[3] = f2s24_fast(t2_r);

        ptx2[0] = f2s24_fast(t3_l);
        ptx2[1] = f2s24_fast(t3_r);
        ptx2[2] = f2s24_fast(t4_l);
        ptx2[3] = f2s24_fast(t4_r);
#endif
        ptx1 += AUDIO_TDM_SLOTS;
        ptx2 += AUDIO_TDM_SLOTS;
    }
}
