#include "Board/board_audio.h"
#include "Board/board_audio_format.h"

#include "sai.h"
#include "tlv320aic3204.h"

#include <arm_acle.h>
#include "stm32h743xx.h"
#include <string.h>
#include "Platform/memory_layout.h"

#define BOARD_AUDIO_INIT_ATTEMPTS 3U
#define BOARD_AUDIO_RETRY_DELAY_MS 10U

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

uint8_t board_audio_start_stream(int32_t *rx_buffer,
                                 int32_t *tx_buffer,
                                 uint32_t word_count,
                                 volatile audio_init_state_t *init_state)
{
    if ((rx_buffer == NULL) || (tx_buffer == NULL) || (word_count == 0U)
            || (word_count > UINT16_MAX) || (init_state == NULL))
    {
        g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_BAD_ARGUMENT;
        g_board_audio_boot_diag.failure_count++;
        if (init_state != NULL) *init_state = AUDIO_INIT_ERROR;
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
            HAL_Delay(BOARD_AUDIO_RETRY_DELAY_MS);
        }

        *init_state = AUDIO_INIT_CODEC;
        g_board_audio_boot_diag.codec_ready = 0U;

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

        /* Hardware-proven post-codec boundary: stop RX, stop TX, start TX, start RX. */
        *init_state = AUDIO_INIT_SAI_SYNC;
        const HAL_StatusTypeDef stop_rx = HAL_SAI_DMAStop(&hsai_BlockB1);
        const HAL_StatusTypeDef stop_tx = HAL_SAI_DMAStop(&hsai_BlockA1);
        g_board_audio_boot_diag.rx_started = 0U;
        g_board_audio_boot_diag.tx_started = 0U;
        if ((stop_rx != HAL_OK) || (stop_tx != HAL_OK))
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_SAI_SYNC;
            g_board_audio_boot_diag.failure_count++;
            continue;
        }

        if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)tx_buffer,
                                 (uint16_t)word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_TX_DMA;
            g_board_audio_boot_diag.failure_count++;
            continue;
        }
        g_board_audio_boot_diag.tx_started = 1U;

        if (HAL_SAI_Receive_DMA(&hsai_BlockB1, (uint8_t *)rx_buffer,
                                (uint16_t)word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_RX_DMA;
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA1);
            g_board_audio_boot_diag.tx_started = 0U;
            continue;
        }

        g_board_audio_boot_diag.rx_started = 1U;
        g_board_audio_boot_diag.stream_started = 1U;
        g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_OK;
        *init_state = AUDIO_INIT_READY;
        return 1U;
    }

    (void)HAL_SAI_DMAStop(&hsai_BlockB1);
    (void)HAL_SAI_DMAStop(&hsai_BlockA1);
    g_board_audio_boot_diag.stream_started = 0U;
    g_board_audio_boot_diag.tx_started = 0U;
    g_board_audio_boot_diag.rx_started = 0U;
    *init_state = AUDIO_INIT_ERROR;
    return 0U;
}

void board_audio_stop_stream(void)
{
    (void)HAL_SAI_DMAStop(&hsai_BlockB1);
    (void)HAL_SAI_DMAStop(&hsai_BlockA1);
    g_board_audio_boot_diag.stream_started = 0U;
    g_board_audio_boot_diag.tx_started = 0U;
    g_board_audio_boot_diag.rx_started = 0U;
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

ITCM_TEXT void board_audio_unpack_input(const int32_t *AUDIO_RESTRICT rx,
                              audio_physical_inputs_t *AUDIO_RESTRICT physical_inputs,
                              uint32_t frames,
                              float in_scale)
{
    float *AUDIO_RESTRICT line_l = physical_inputs->line.left;
    float *AUDIO_RESTRICT line_r = physical_inputs->line.right;
    const int32_t *AUDIO_RESTRICT prx = rx;
    for (uint32_t n = 0; n < frames; n++)
    {
        const float left = s242f_fast(prx[0], in_scale);
        const float right = s242f_fast(prx[1], in_scale);
        line_l[n] = left;
        line_r[n] = right;
        prx += BOARD_AUDIO_TDM_SLOTS;
    }

    memset(physical_inputs->mic.mono, 0, frames * sizeof(float));
}

ITCM_TEXT void board_audio_pack_output(int32_t *AUDIO_RESTRICT tx,
                             const float *AUDIO_RESTRICT main_l,
                             const float *AUDIO_RESTRICT main_r,
                             uint32_t frames)
{
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
