#include "Board/board_audio.h"
#include "Board/board_audio_format.h"
#include "Audio/audio_track_diag.h"
#include "Storage/cache_maintenance.h"
#include "Storage/memory_layout.h"

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

void board_audio_get_runtime_diag(board_audio_runtime_diag_t *out_diag)
{
    if (out_diag == NULL) return;
    *out_diag = (board_audio_runtime_diag_t){0};
    out_diag->tx_sai_state = (uint32_t)hsai_BlockA1.State;
    out_diag->rx_sai_state = (uint32_t)hsai_BlockB1.State;
    out_diag->tx_sai_error_code = hsai_BlockA1.ErrorCode;
    out_diag->rx_sai_error_code = hsai_BlockB1.ErrorCode;
    out_diag->tx_dma_state = (hsai_BlockA1.hdmatx != NULL) ? (uint32_t)hsai_BlockA1.hdmatx->State : 0U;
    out_diag->rx_dma_state = (hsai_BlockB1.hdmarx != NULL) ? (uint32_t)hsai_BlockB1.hdmarx->State : 0U;
    out_diag->tx_dma_error_code = (hsai_BlockA1.hdmatx != NULL) ? hsai_BlockA1.hdmatx->ErrorCode : 0U;
    out_diag->rx_dma_error_code = (hsai_BlockB1.hdmarx != NULL) ? hsai_BlockB1.hdmarx->ErrorCode : 0U;
    out_diag->frame_length = hsai_BlockA1.FrameInit.FrameLength;
    out_diag->active_frame_length = hsai_BlockA1.FrameInit.ActiveFrameLength;
    out_diag->data_size = hsai_BlockA1.Init.DataSize;
    out_diag->slot_size = hsai_BlockA1.SlotInit.SlotSize;
    out_diag->slot_number = hsai_BlockA1.SlotInit.SlotNumber;
    out_diag->slot_active = hsai_BlockA1.SlotInit.SlotActive;
}

uint8_t board_audio_restart_stream(int32_t *rx_buffer,
                                   int32_t *tx_buffer,
                                   uint32_t word_count,
                                   board_audio_restart_diag_t *out_diag)
{
    board_audio_restart_diag_t diag = {0};
    diag.supported = 1U;
    diag.stop_rx_status = 0xFFU;
    diag.stop_tx_status = 0xFFU;
    diag.start_tx_status = 0xFFU;
    diag.start_rx_status = 0xFFU;
    diag.word_count = word_count;

    if ((rx_buffer == NULL) || (tx_buffer == NULL)
            || (word_count == 0U) || (word_count > UINT16_MAX))
    {
        if (out_diag != NULL) *out_diag = diag;
        return 0U;
    }

    board_audio_get_runtime_diag(&diag.before);
    diag.tx_sr_before = hsai_BlockA1.Instance->SR;
    diag.rx_sr_before = hsai_BlockB1.Instance->SR;
    diag.tx_cr1_before = hsai_BlockA1.Instance->CR1;
    diag.rx_cr1_before = hsai_BlockB1.Instance->CR1;

    /* RX owns the audio callbacks: stop it first, then stop the TX clock master. */
    diag.stop_rx_status = (uint8_t)HAL_SAI_DMAStop(&hsai_BlockB1);
    diag.stop_tx_status = (uint8_t)HAL_SAI_DMAStop(&hsai_BlockA1);

    /* DMAStop already flushes each FIFO; repeat explicitly and clear stale flags. */
    SET_BIT(hsai_BlockA1.Instance->CR2, SAI_xCR2_FFLUSH);
    SET_BIT(hsai_BlockB1.Instance->CR2, SAI_xCR2_FFLUSH);
    hsai_BlockA1.Instance->CLRFR = 0xFFFFFFFFU;
    hsai_BlockB1.Instance->CLRFR = 0xFFFFFFFFU;
    diag.fifo_flushed = 1U;
    diag.flags_cleared = 1U;
    __DSB();

    board_audio_get_runtime_diag(&diag.after_purge);
    diag.tx_sr_after_purge = hsai_BlockA1.Instance->SR;
    diag.rx_sr_after_purge = hsai_BlockB1.Instance->SR;

    if ((diag.stop_rx_status != (uint8_t)HAL_OK)
            || (diag.stop_tx_status != (uint8_t)HAL_OK))
    {
        if (out_diag != NULL) *out_diag = diag;
        return 0U;
    }

    /* Both ping-pong halves are known silence before the first restarted frame. */
    const size_t buffer_bytes = (size_t)word_count * sizeof(int32_t);
    memset(rx_buffer, 0, buffer_bytes);
    memset(tx_buffer, 0, buffer_bytes);
    dcache_invalidate_by_addr_aligned(rx_buffer, buffer_bytes);
#if AUDIO_DMA_BUFFER_IS_CACHEABLE
    dcache_clean_by_addr_aligned(tx_buffer, buffer_bytes);
#endif
    diag.buffers_zeroed = 1U;
    __DSB();

    /* Re-establish the original production order without any codec I2C access. */
    diag.start_tx_status = (uint8_t)HAL_SAI_Transmit_DMA(
        &hsai_BlockA1, (uint8_t *)tx_buffer, (uint16_t)word_count);
    if (diag.start_tx_status == (uint8_t)HAL_OK)
    {
        diag.start_rx_status = (uint8_t)HAL_SAI_Receive_DMA(
            &hsai_BlockB1, (uint8_t *)rx_buffer, (uint16_t)word_count);
    }

    if (diag.start_rx_status != (uint8_t)HAL_OK)
    {
        (void)HAL_SAI_DMAStop(&hsai_BlockB1);
        (void)HAL_SAI_DMAStop(&hsai_BlockA1);
    }

    board_audio_get_runtime_diag(&diag.after_restart);
    diag.tx_sr_after_restart = hsai_BlockA1.Instance->SR;
    diag.rx_sr_after_restart = hsai_BlockB1.Instance->SR;
    diag.tx_cr1_after_restart = hsai_BlockA1.Instance->CR1;
    diag.rx_cr1_after_restart = hsai_BlockB1.Instance->CR1;
    diag.success = ((diag.start_tx_status == (uint8_t)HAL_OK)
                 && (diag.start_rx_status == (uint8_t)HAL_OK)) ? 1U : 0U;

    if (out_diag != NULL) *out_diag = diag;
    return diag.success;
}

static void board_audio_codec_snapshot_read(board_audio_codec_snapshot_t *snapshot,
                                            board_audio_codec_reg_id_t id,
                                            uint8_t page,
                                            uint8_t reg,
                                            uint8_t expected)
{
    snapshot->expected[id] = expected;
    uint8_t actual = 0U;
    const tlv320aic3204_status_t status = TLV320AIC3204_ReadReg(
        &hi2c1, TLV320AIC3204_I2C_ADDR_7BIT, page, reg, &actual);
    if (status == TLV320AIC3204_STATUS_OK)
    {
        snapshot->actual[id] = actual;
        snapshot->valid_mask |= (1UL << (uint32_t)id);
    }
    else
    {
        snapshot->read_ok = 0U;
        snapshot->i2c_error = 1U;
    }
}

static void board_audio_codec_snapshot_read_current_page(board_audio_codec_snapshot_t *snapshot,
                                                         board_audio_codec_reg_id_t id,
                                                         uint8_t reg,
                                                         uint8_t expected)
{
    snapshot->expected[id] = expected;
    uint8_t actual = 0U;
    const tlv320aic3204_status_t status = TLV320AIC3204_ReadRegCurrentPage(
        &hi2c1, TLV320AIC3204_I2C_ADDR_7BIT, reg, &actual);
    if (status == TLV320AIC3204_STATUS_OK)
    {
        snapshot->actual[id] = actual;
        snapshot->valid_mask |= (1UL << (uint32_t)id);
    }
    else
    {
        snapshot->read_ok = 0U;
        snapshot->i2c_error = 1U;
    }
}

void board_audio_get_codec_post_test_snapshot(board_audio_codec_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) return;
    *out_snapshot = (board_audio_codec_snapshot_t){0};
    out_snapshot->read_ok = 1U;

    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_INTERFACE,
                                    0U, 27U, 0x20U);
    /* Only the page selector is changed to address the second register page. */
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_0,
                                                 4U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_1,
                                                 11U, 0x81U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_2,
                                                 12U, 0x82U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_3,
                                                 13U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_4,
                                                 14U, 0x80U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_5,
                                                 18U, 0x81U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_6,
                                                 19U, 0x82U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_7,
                                                 20U, 0x80U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_DAC_STATE,
                                                 37U, 0xAAU);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_L,
                                                 65U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_R,
                                                 66U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_STATUS,
                                                 36U, 0x44U);

    /* Page 1 is selected once; all remaining reads are read-only. */
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_MUTE,
                                    1U, 16U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_ROUTE_L,
                                                 12U, 0x08U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_ROUTE_R,
                                                 13U, 0x08U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_OUTPUT_POWER,
                                                 9U, 0x30U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_ANALOG_VOLUME_L,
                                                 16U, 0x00U);
    board_audio_codec_snapshot_read_current_page(out_snapshot, BOARD_AUDIO_CODEC_REG_ANALOG_VOLUME_R,
                                                 17U, 0x00U);
    out_snapshot->expected[BOARD_AUDIO_CODEC_REG_FUNCTIONAL_MODE] = 0U;
    out_snapshot->expected[BOARD_AUDIO_CODEC_REG_CHIP_ID] = 0U;
}

uint8_t board_audio_codec_reset_and_reinit(board_audio_codec_reset_diag_t *out_diag)
{
    board_audio_codec_reset_diag_t diag = {0};
    tlv320aic3204_diag_t codec_diag;
    diag.supported = 1U;
    diag.reset_type = BOARD_AUDIO_CODEC_RESET_SOFTWARE;
    const tlv320aic3204_status_t status = TLV320AIC3204_InitDefaultChecked();
    TLV320AIC3204_GetDiag(&codec_diag);
    diag.reset_ok = codec_diag.reset_ok;
    diag.init_ok = (status == TLV320AIC3204_STATUS_OK) ? 1U : 0U;
    diag.reset_type = (codec_diag.reset_pin_used != 0U)
        ? BOARD_AUDIO_CODEC_RESET_HARDWARE : BOARD_AUDIO_CODEC_RESET_SOFTWARE;
    diag.reset_pin_used = codec_diag.reset_pin_used;
    diag.reset_low_duration_ms = codec_diag.reset_low_duration_ms;
    diag.wait_ms = codec_diag.reset_wait_ms;
    diag.i2c_errors = codec_diag.i2c_errors;
    diag.write_failures = codec_diag.write_failures;
    diag.readback_errors = codec_diag.readback_errors;
    board_audio_capture_codec_diag();
    if (out_diag != NULL) *out_diag = diag;
    return diag.init_ok;
}

uint8_t board_audio_is_tx_callback_handle(void *handle)
{
    return ((handle != NULL) && (handle == (void *)&hsai_BlockA1)) ? 1U : 0U;
}

uint8_t board_audio_is_audio_dma_handle(void *handle)
{
    return ((handle != NULL)
            && ((handle == (void *)hsai_BlockA1.hdmatx)
            || (handle == (void *)hsai_BlockA1.hdmarx)
            || (handle == (void *)hsai_BlockB1.hdmatx)
            || (handle == (void *)hsai_BlockB1.hdmarx))) ? 1U : 0U;
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
    const uint8_t diag_enabled = audio_track_diag_is_enabled();
    for (uint32_t n = 0; n < frames; n++)
    {
#if defined(USE_F2S24_SSAT)
        ptx[0] = f2s24_fast_ssat(main_l[n]);
        ptx[1] = f2s24_fast_ssat(main_r[n]);
#else
        ptx[0] = f2s24_fast(main_l[n]);
        ptx[1] = f2s24_fast(main_r[n]);
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
