#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Audio/audio_track_diag.h"

#include "cs42448.h"
#include "sai.h"

#include <arm_acle.h>
#include "stm32h743xx.h"
#include <string.h>

#define CS42448_I2C_ADDRESS 0x48U
#define BOARD_AUDIO_INIT_ATTEMPTS 2U

static board_audio_boot_diag_t g_board_audio_boot_diag;

void board_audio_codec_init(void)
{
    g_board_audio_boot_diag = (board_audio_boot_diag_t){0};
}

void board_audio_init(void)
{
}

uint8_t board_audio_start_stream(int32_t *rx_buffer, int32_t *tx_buffer, uint32_t word_count)
{
    if ((rx_buffer == NULL) || (tx_buffer == NULL) || (word_count == 0U))
    {
        g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_BAD_ARGUMENT;
        g_board_audio_boot_diag.failure_count++;
        return 0U;
    }

    g_board_audio_boot_diag.init_count++;
    g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_OK;
    g_board_audio_boot_diag.codec_ready = 0U;
    g_board_audio_boot_diag.stream_started = 0U;

    for (uint32_t attempt = 0U; attempt < BOARD_AUDIO_INIT_ATTEMPTS; ++attempt)
    {
        if (attempt != 0U)
        {
            g_board_audio_boot_diag.retry_count++;
        }

        (void)HAL_SAI_DMAStop(&hsai_BlockB2);
        (void)HAL_SAI_DMAStop(&hsai_BlockA2);

        /*
         * CS42448 requires stable MCLK/LRCK while leaving reset and for its
         * 2000-LRCK-cycle startup.  Start only the zero-filled TX clock DMA;
         * RX (the IRQ authority) remains stopped until the codec is verified.
         */
        if (HAL_SAI_Transmit_DMA(&hsai_BlockA2, (uint8_t *)tx_buffer, word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_TX_DMA;
            g_board_audio_boot_diag.failure_count++;
            continue;
        }
        HAL_Delay(1U);

        const cs42448_status_t codec_status = CS42448_Init(CS42448_I2C_ADDRESS);
        if (codec_status != CS42448_STATUS_OK)
        {
            if (codec_status == CS42448_STATUS_NOT_FOUND)
            {
                g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_CODEC_NOT_FOUND;
            }
            else if (codec_status == CS42448_STATUS_VERIFY)
            {
                g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_VERIFY;
            }
            else if (codec_status == CS42448_STATUS_CLOCK)
            {
                g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_READY_TIMEOUT;
            }
            else
            {
                g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_I2C;
            }
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA2);
            continue;
        }

        g_board_audio_boot_diag.codec_ready = 1U;
        if (HAL_SAI_Receive_DMA(&hsai_BlockB2, (uint8_t *)rx_buffer, word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_RX_DMA;
            g_board_audio_boot_diag.codec_ready = 0U;
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA2);
            continue;
        }

        g_board_audio_boot_diag.stream_started = 1U;
        return 1U;
    }

    return 0U;
}

void board_audio_get_boot_diag(board_audio_boot_diag_t *out_diag)
{
    if (out_diag != NULL)
    {
        *out_diag = g_board_audio_boot_diag;
    }
}

uint8_t board_audio_is_rx_callback_handle(void *handle)
{
    return (handle == (void *)&hsai_BlockB2) ? 1U : 0U;
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

    if (tr0_on == 0U)
    {
        memset(tr0_l, 0, frames * sizeof(float));
        memset(tr0_r, 0, frames * sizeof(float));
    }
    if (tr1_on == 0U)
    {
        memset(tr1_l, 0, frames * sizeof(float));
        memset(tr1_r, 0, frames * sizeof(float));
    }
    if (tr2_on == 0U)
    {
        memset(tr2_l, 0, frames * sizeof(float));
        memset(tr2_r, 0, frames * sizeof(float));
    }
    if (tr3_on != 0U)
    {
        memset(tr3_l, 0, frames * sizeof(float));
        memset(tr3_r, 0, frames * sizeof(float));
    }

    if ((tr0_on | tr1_on | tr2_on) == 0U)
    {
        return;
    }

    const int32_t *AUDIO_RESTRICT prx = rx;

    for (uint32_t n = 0; n < frames; n++)
    {
        const int32_t s0 = prx[0];
        const int32_t s1 = prx[1];
        const int32_t s2 = prx[2];
        const int32_t s3 = prx[3];
        const int32_t s4 = prx[4];
        const int32_t s5 = prx[5];

        if (tr0_on != 0U)
        {
            /* PCB codec input is wired L/R-reversed: restore the internal order here. */
            tr0_l[n] = s242f_fast(s1, in_scale);
            tr0_r[n] = s242f_fast(s0, in_scale);
        }
        if (tr1_on != 0U)
        {
            tr1_l[n] = s242f_fast(s3, in_scale);
            tr1_r[n] = s242f_fast(s2, in_scale);
        }
        if (tr2_on != 0U)
        {
            tr2_l[n] = s242f_fast(s5, in_scale);
            tr2_r[n] = s242f_fast(s4, in_scale);
        }

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
    int32_t *AUDIO_RESTRICT ptx = tx;
    const float gain_step = (cue_gain_end - cue_gain_start) / (float)frames;
    float cue_gain = cue_gain_start;
    const uint8_t diag_enabled = audio_track_diag_is_enabled();

    for (uint32_t n = 0; n < frames; n++)
    {
        const float main_sample_l = main_l[n];
        const float main_sample_r = main_r[n];
        const float cue_sample_l = cue_l[n] * cue_gain;
        const float cue_sample_r = cue_r[n] * cue_gain;

#if defined(USE_F2S24_SSAT)
        /* PCB codec output is wired L/R-reversed: swap every stereo SAI pair. */
        ptx[0] = f2s24_fast_ssat(main_sample_r);
        ptx[1] = f2s24_fast_ssat(main_sample_l);
        ptx[2] = f2s24_fast_ssat(cue_sample_r);
        ptx[3] = f2s24_fast_ssat(cue_sample_l);
        ptx[4] = f2s24_fast_ssat(main_sample_r);
        ptx[5] = f2s24_fast_ssat(main_sample_l);
#else
        ptx[0] = f2s24_fast(main_sample_r);
        ptx[1] = f2s24_fast(main_sample_l);
        ptx[2] = f2s24_fast(cue_sample_r);
        ptx[3] = f2s24_fast(cue_sample_l);
        ptx[4] = f2s24_fast(main_sample_r);
        ptx[5] = f2s24_fast(main_sample_l);
#endif
        if (diag_enabled != 0U)
        {
            const float clipped_l = (main_sample_l < -1.0f) ? -1.0f
                : ((main_sample_l > 0.9999998807907104f) ? 0.9999998807907104f : main_sample_l);
            const float clipped_r = (main_sample_r < -1.0f) ? -1.0f
                : ((main_sample_r > 0.9999998807907104f) ? 0.9999998807907104f : main_sample_r);
            audio_global_diag_report_final_pcm24(main_sample_l, clipped_l);
            audio_global_diag_report_final_pcm24(main_sample_r, clipped_r);
            audio_global_diag_measure_sample(AUDIO_GLOBAL_DIAG_DMA_MAIN,
                (float)s24_sign_extend(ptx[0]) * (1.0f / 8388607.0f),
                (float)s24_sign_extend(ptx[1]) * (1.0f / 8388607.0f));
        }
        ptx[6] = 0;
        ptx[7] = 0;
        ptx += BOARD_AUDIO_TDM_SLOTS;
        cue_gain += gain_step;
    }
}
