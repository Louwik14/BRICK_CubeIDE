#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Audio/audio_track_diag.h"

#include "i2c.h"
#include "sai.h"
#include "tlv320aic3204.h"

#include <arm_acle.h>
#include "stm32h743xx.h"
#include <string.h>

#define BOARD_AUDIO_INIT_ATTEMPTS 2U

static board_audio_boot_diag_t g_board_audio_boot_diag;

static void board_audio_capture_codec_diag(void)
{
    tlv320aic3204_diag_t codec_diag;
    TLV320AIC3204_GetDiag(&codec_diag);
    g_board_audio_boot_diag.codec_stage = (uint8_t)codec_diag.stage;
    g_board_audio_boot_diag.codec_page = codec_diag.page;
    g_board_audio_boot_diag.codec_reg = codec_diag.reg;
    g_board_audio_boot_diag.codec_expected = codec_diag.expected;
    g_board_audio_boot_diag.codec_mask = codec_diag.mask;
    g_board_audio_boot_diag.codec_actual = codec_diag.actual;
    g_board_audio_boot_diag.reset_ok = codec_diag.reset_ok;
    g_board_audio_boot_diag.clocks_ok = codec_diag.clocks_ok;
    g_board_audio_boot_diag.interface_ok = codec_diag.interface_ok;
    g_board_audio_boot_diag.dac_powered = codec_diag.dac_powered;
    g_board_audio_boot_diag.dac_routed = codec_diag.dac_routed;
    g_board_audio_boot_diag.dac_unmuted = codec_diag.dac_unmuted;
    g_board_audio_boot_diag.output_routed = codec_diag.output_routed;
    g_board_audio_boot_diag.output_powered = codec_diag.output_powered;
    g_board_audio_boot_diag.output_unmuted = codec_diag.output_unmuted;
    g_board_audio_boot_diag.volume_ok = codec_diag.volume_ok;
}

static board_audio_boot_error_t board_audio_codec_error(tlv320aic3204_status_t status)
{
    tlv320aic3204_diag_t codec_diag;
    TLV320AIC3204_GetDiag(&codec_diag);

    if ((status == TLV320AIC3204_STATUS_NOT_FOUND) ||
        (codec_diag.stage == TLV320AIC3204_STAGE_DEVICE_ACK))
    {
        return BOARD_AUDIO_BOOT_CODEC_NOT_FOUND;
    }
    if (codec_diag.stage == TLV320AIC3204_STAGE_RESET)
    {
        return BOARD_AUDIO_BOOT_CODEC_RESET;
    }
    if ((status == TLV320AIC3204_STATUS_READY_TIMEOUT) &&
        (codec_diag.stage == TLV320AIC3204_STAGE_OUTPUT_READY))
    {
        return BOARD_AUDIO_BOOT_OUTPUT_POWER;
    }
    if (status == TLV320AIC3204_STATUS_READY_TIMEOUT)
    {
        return BOARD_AUDIO_BOOT_CLOCK;
    }
    if ((codec_diag.stage == TLV320AIC3204_STAGE_OUTPUT_MUTE) ||
        (codec_diag.stage == TLV320AIC3204_STAGE_OUTPUT_UNMUTE))
    {
        return BOARD_AUDIO_BOOT_CODEC_MUTED;
    }
    if (codec_diag.stage == TLV320AIC3204_STAGE_DAC_ROUTE)
    {
        return BOARD_AUDIO_BOOT_DAC_ROUTE;
    }
    if (codec_diag.stage == TLV320AIC3204_STAGE_OUTPUT_ROUTE)
    {
        return BOARD_AUDIO_BOOT_OUTPUT_ROUTE;
    }
    if (codec_diag.stage == TLV320AIC3204_STAGE_DAC_VOLUME)
    {
        return BOARD_AUDIO_BOOT_VOLUME;
    }
    if (status == TLV320AIC3204_STATUS_VERIFY_ERROR)
    {
        return BOARD_AUDIO_BOOT_VERIFY;
    }
    return BOARD_AUDIO_BOOT_I2C;
}

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
    g_board_audio_boot_diag.tx_started = 0U;
    g_board_audio_boot_diag.rx_started = 0U;

    for (uint32_t attempt = 0U; attempt < BOARD_AUDIO_INIT_ATTEMPTS; ++attempt)
    {
        if (attempt != 0U)
        {
            g_board_audio_boot_diag.retry_count++;
        }

        (void)HAL_SAI_DMAStop(&hsai_BlockB1);
        (void)HAL_SAI_DMAStop(&hsai_BlockA1);

        /*
         * Run only the zero-filled TX DMA to establish MCLK/BCLK/WCLK while
         * reset, clock dividers and analog blocks are configured.  RX is the
         * audio IRQ authority and remains stopped until verification passes.
         */
        if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)tx_buffer, word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_TX_DMA;
            g_board_audio_boot_diag.failure_count++;
            continue;
        }
        g_board_audio_boot_diag.tx_started = 1U;
        HAL_Delay(1U);

        const tlv320aic3204_status_t codec_status = TLV320AIC3204_InitDefault();
        board_audio_capture_codec_diag();
        if (codec_status != TLV320AIC3204_STATUS_OK)
        {
            g_board_audio_boot_diag.last_error = board_audio_codec_error(codec_status);
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA1);
            g_board_audio_boot_diag.tx_started = 0U;
            continue;
        }

        g_board_audio_boot_diag.codec_ready = 1U;
        if (HAL_SAI_Receive_DMA(&hsai_BlockB1, (uint8_t *)rx_buffer, word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_RX_DMA;
            g_board_audio_boot_diag.codec_ready = 0U;
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA1);
            g_board_audio_boot_diag.tx_started = 0U;
            continue;
        }

        g_board_audio_boot_diag.rx_started = 1U;
        g_board_audio_boot_diag.stream_started = 1U;
        g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_OK;
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
        /* PCB codec input is wired L/R-reversed: restore the internal order here. */
        tr0_l[n] = s242f_fast(prx[1], in_scale);
        tr0_r[n] = s242f_fast(prx[0], in_scale);
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
    const uint8_t diag_enabled = audio_track_diag_is_enabled();
    for (uint32_t n = 0; n < frames; n++)
    {
#if defined(USE_F2S24_SSAT)
        /* PCB codec output is wired L/R-reversed: swap only at the SAI boundary. */
        ptx[0] = f2s24_fast_ssat(main_r[n]);
        ptx[1] = f2s24_fast_ssat(main_l[n]);
#else
        ptx[0] = f2s24_fast(main_r[n]);
        ptx[1] = f2s24_fast(main_l[n]);
#endif
        if (diag_enabled != 0U)
        {
            const float clipped_l = (main_l[n] < -1.0f) ? -1.0f
                : ((main_l[n] > 0.9999998807907104f) ? 0.9999998807907104f : main_l[n]);
            const float clipped_r = (main_r[n] < -1.0f) ? -1.0f
                : ((main_r[n] > 0.9999998807907104f) ? 0.9999998807907104f : main_r[n]);
            audio_global_diag_report_final_pcm24(main_l[n], clipped_l);
            audio_global_diag_report_final_pcm24(main_r[n], clipped_r);
            audio_global_diag_measure_sample(AUDIO_GLOBAL_DIAG_DMA_MAIN,
                (float)s24_sign_extend(ptx[0]) * (1.0f / 8388607.0f),
                (float)s24_sign_extend(ptx[1]) * (1.0f / 8388607.0f));
        }
        ptx += BOARD_AUDIO_TDM_SLOTS;
    }
}
