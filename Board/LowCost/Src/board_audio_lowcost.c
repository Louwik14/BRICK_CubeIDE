#include "Board/board_audio.h"
#include "Board/board_audio_format.h"

#include "i2c.h"
#include "sai.h"
#include "tlv320aic3204.h"

#include <arm_acle.h>
#include "stm32h743xx.h"
#include <string.h>

void board_audio_codec_init(void)
{
    (void)TLV320AIC3204_InitDefault();
}

void board_audio_init(void)
{
}

uint8_t board_audio_start_stream(int32_t *rx_buffer, int32_t *tx_buffer, uint32_t word_count)
{
    if ((rx_buffer == NULL) || (tx_buffer == NULL) || (word_count == 0U))
    {
        return 0U;
    }

    if (HAL_SAI_Receive_DMA(&hsai_BlockB1, (uint8_t *)rx_buffer, word_count) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)tx_buffer, word_count) != HAL_OK)
    {
        return 0U;
    }

    return 1U;
}

uint8_t board_audio_is_rx_callback_handle(void *handle)
{
    return (handle == (void *)&hsai_BlockB1) ? 1U : 0U;
}

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

void board_audio_unpack_input(const int32_t *AUDIO_RESTRICT rx,
                              StereoTrack *AUDIO_RESTRICT track_buf,
                              uint32_t frames,
                              float in_scale)
{
    const uint32_t tr0_on = (uint32_t)track_buf[0].enabled;
    float *AUDIO_RESTRICT tr0_l = track_buf[0].L;
    float *AUDIO_RESTRICT tr0_r = track_buf[0].R;

    if (tr0_on == 0U)
    {
        memset(tr0_l, 0, frames * sizeof(float));
        memset(tr0_r, 0, frames * sizeof(float));
    }

    for (uint32_t tr = 1U; tr < 4U; tr++)
    {
        memset(track_buf[tr].L, 0, frames * sizeof(float));
        memset(track_buf[tr].R, 0, frames * sizeof(float));
    }

    if (tr0_on == 0U)
    {
        return;
    }

    const int32_t *AUDIO_RESTRICT prx = rx;
    for (uint32_t n = 0; n < frames; n++)
    {
        tr0_l[n] = s242f_fast(prx[0], in_scale);
        tr0_r[n] = s242f_fast(prx[1], in_scale);
        prx += BOARD_AUDIO_TDM_SLOTS;
    }
}

void board_audio_pack_output(int32_t *AUDIO_RESTRICT tx,
                             const float *AUDIO_RESTRICT main_l,
                             const float *AUDIO_RESTRICT main_r,
                             const float *AUDIO_RESTRICT cue_l,
                             const float *AUDIO_RESTRICT cue_r,
                             uint32_t frames,
                             float cue_gain_start,
                             float cue_gain_end)
{
    (void)cue_l;
    (void)cue_r;
    (void)cue_gain_start;
    (void)cue_gain_end;

    int32_t *AUDIO_RESTRICT ptx = tx;
    for (uint32_t n = 0; n < frames; n++)
    {
#if defined(USE_F2S24_SSAT)
        ptx[0] = f2s24_fast_ssat(main_l[n]);
        ptx[1] = f2s24_fast_ssat(main_r[n]);
#else
        ptx[0] = f2s24_fast(main_l[n]);
        ptx[1] = f2s24_fast(main_r[n]);
#endif
        ptx += BOARD_AUDIO_TDM_SLOTS;
    }
}
