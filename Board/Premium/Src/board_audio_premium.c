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
    g_board_audio_boot_diag.tx_started = 0U;
    g_board_audio_boot_diag.rx_started = 0U;

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
        g_board_audio_boot_diag.tx_started = 1U;
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
            g_board_audio_boot_diag.tx_started = 0U;
            continue;
        }

        g_board_audio_boot_diag.codec_ready = 1U;
        g_board_audio_boot_diag.reset_ok = 1U;
        g_board_audio_boot_diag.clocks_ok = 1U;
        g_board_audio_boot_diag.interface_ok = 1U;
        g_board_audio_boot_diag.dac_powered = 1U;
        g_board_audio_boot_diag.dac_routed = 1U;
        g_board_audio_boot_diag.dac_unmuted = 1U;
        g_board_audio_boot_diag.output_routed = 1U;
        g_board_audio_boot_diag.output_powered = 1U;
        g_board_audio_boot_diag.output_unmuted = 1U;
        g_board_audio_boot_diag.volume_ok = 1U;
        if (HAL_SAI_Receive_DMA(&hsai_BlockB2, (uint8_t *)rx_buffer, word_count) != HAL_OK)
        {
            g_board_audio_boot_diag.last_error = BOARD_AUDIO_BOOT_RX_DMA;
            g_board_audio_boot_diag.codec_ready = 0U;
            g_board_audio_boot_diag.failure_count++;
            (void)HAL_SAI_DMAStop(&hsai_BlockA2);
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
    out_diag->tx_sai_state = (uint32_t)hsai_BlockA2.State;
    out_diag->rx_sai_state = (uint32_t)hsai_BlockB2.State;
    out_diag->tx_sai_error_code = hsai_BlockA2.ErrorCode;
    out_diag->rx_sai_error_code = hsai_BlockB2.ErrorCode;
    out_diag->tx_dma_state = (hsai_BlockA2.hdmatx != NULL) ? (uint32_t)hsai_BlockA2.hdmatx->State : 0U;
    out_diag->rx_dma_state = (hsai_BlockB2.hdmarx != NULL) ? (uint32_t)hsai_BlockB2.hdmarx->State : 0U;
    out_diag->tx_dma_error_code = (hsai_BlockA2.hdmatx != NULL) ? hsai_BlockA2.hdmatx->ErrorCode : 0U;
    out_diag->rx_dma_error_code = (hsai_BlockB2.hdmarx != NULL) ? hsai_BlockB2.hdmarx->ErrorCode : 0U;
    out_diag->frame_length = hsai_BlockA2.FrameInit.FrameLength;
    out_diag->active_frame_length = hsai_BlockA2.FrameInit.ActiveFrameLength;
    out_diag->data_size = hsai_BlockA2.Init.DataSize;
    out_diag->slot_size = hsai_BlockA2.SlotInit.SlotSize;
    out_diag->slot_number = hsai_BlockA2.SlotInit.SlotNumber;
    out_diag->slot_active = hsai_BlockA2.SlotInit.SlotActive;
}

static void board_audio_codec_snapshot_read(board_audio_codec_snapshot_t *snapshot,
                                            board_audio_codec_reg_id_t id,
                                            uint8_t reg,
                                            uint8_t expected)
{
    snapshot->expected[id] = expected;
    uint8_t actual = 0U;
    const cs42448_status_t status = CS42448_ReadReg(CS42448_I2C_ADDR, reg, &actual);
    if (status == CS42448_STATUS_OK)
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
                                    0x04U, 0x76U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_0,
                                    0x03U, 0xF4U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_CLOCK_1,
                                    0x19U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_DAC_STATE,
                                    0x02U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_MUTE,
                                    0x07U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_OUTPUT_POWER,
                                    0x02U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_L,
                                    0x08U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_R,
                                    0x09U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_STATUS,
                                    0x19U, 0x00U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_STATUS_MASK,
                                    0x1AU, 0x18U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_FUNCTIONAL_MODE,
                                    0x03U, 0xF4U);
    board_audio_codec_snapshot_read(out_snapshot, BOARD_AUDIO_CODEC_REG_CHIP_ID,
                                    0x01U, 0x00U);
}

uint8_t board_audio_is_tx_callback_handle(void *handle)
{
    return ((handle != NULL) && (handle == (void *)&hsai_BlockA2)) ? 1U : 0U;
}

uint8_t board_audio_is_audio_dma_handle(void *handle)
{
    return ((handle != NULL)
            && ((handle == (void *)hsai_BlockA2.hdmatx)
            || (handle == (void *)hsai_BlockA2.hdmarx)
            || (handle == (void *)hsai_BlockB2.hdmatx)
            || (handle == (void *)hsai_BlockB2.hdmarx))) ? 1U : 0U;
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
            tr0_l[n] = s242f_fast(s0, in_scale);
            tr0_r[n] = s242f_fast(s1, in_scale);
        }
        if (tr1_on != 0U)
        {
            tr1_l[n] = s242f_fast(s2, in_scale);
            tr1_r[n] = s242f_fast(s3, in_scale);
        }
        if (tr2_on != 0U)
        {
            tr2_l[n] = s242f_fast(s4, in_scale);
            tr2_r[n] = s242f_fast(s5, in_scale);
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
        ptx[0] = f2s24_fast_ssat(main_sample_l);
        ptx[1] = f2s24_fast_ssat(main_sample_r);
        ptx[2] = f2s24_fast_ssat(cue_sample_l);
        ptx[3] = f2s24_fast_ssat(cue_sample_r);
        ptx[4] = f2s24_fast_ssat(main_sample_l);
        ptx[5] = f2s24_fast_ssat(main_sample_r);
#else
        ptx[0] = f2s24_fast(main_sample_l);
        ptx[1] = f2s24_fast(main_sample_r);
        ptx[2] = f2s24_fast(cue_sample_l);
        ptx[3] = f2s24_fast(cue_sample_r);
        ptx[4] = f2s24_fast(main_sample_l);
        ptx[5] = f2s24_fast(main_sample_r);
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
