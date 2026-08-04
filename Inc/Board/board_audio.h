#pragma once

#include "audio_float.h"

#include <stdint.h>

typedef enum
{
    BOARD_AUDIO_BOOT_OK = 0,
    BOARD_AUDIO_BOOT_BAD_ARGUMENT,
    BOARD_AUDIO_BOOT_CODEC_NOT_FOUND,
    BOARD_AUDIO_BOOT_CODEC_RESET,
    BOARD_AUDIO_BOOT_I2C,
    BOARD_AUDIO_BOOT_VERIFY,
    BOARD_AUDIO_BOOT_READY_TIMEOUT,
    BOARD_AUDIO_BOOT_CLOCK,
    BOARD_AUDIO_BOOT_CODEC_MUTED,
    BOARD_AUDIO_BOOT_DAC_ROUTE,
    BOARD_AUDIO_BOOT_OUTPUT_ROUTE,
    BOARD_AUDIO_BOOT_OUTPUT_POWER,
    BOARD_AUDIO_BOOT_VOLUME,
    BOARD_AUDIO_BOOT_TX_DMA,
    BOARD_AUDIO_BOOT_RX_DMA
} board_audio_boot_error_t;

typedef struct
{
    uint32_t init_count;
    uint32_t failure_count;
    uint32_t retry_count;
    board_audio_boot_error_t last_error;
    uint8_t codec_ready;
    uint8_t stream_started;
    uint8_t tx_started;
    uint8_t rx_started;
    uint8_t codec_stage;
    uint8_t codec_page;
    uint8_t codec_reg;
    uint8_t codec_expected;
    uint8_t codec_mask;
    uint8_t codec_actual;
    uint8_t reset_ok;
    uint8_t clocks_ok;
    uint8_t interface_ok;
    uint8_t dac_powered;
    uint8_t dac_routed;
    uint8_t dac_unmuted;
    uint8_t output_routed;
    uint8_t output_powered;
    uint8_t output_unmuted;
    uint8_t volume_ok;
} board_audio_boot_diag_t;

typedef struct
{
    uint32_t tx_sai_state;
    uint32_t rx_sai_state;
    uint32_t tx_sai_error_code;
    uint32_t rx_sai_error_code;
    uint32_t tx_dma_state;
    uint32_t rx_dma_state;
    uint32_t tx_dma_error_code;
    uint32_t rx_dma_error_code;
    uint32_t frame_length;
    uint32_t active_frame_length;
    uint32_t data_size;
    uint32_t slot_size;
    uint32_t slot_number;
    uint32_t slot_active;
} board_audio_runtime_diag_t;

#define BOARD_AUDIO_CODEC_SNAPSHOT_REG_COUNT 24U

typedef enum
{
    BOARD_AUDIO_CODEC_REG_INTERFACE = 0U,
    BOARD_AUDIO_CODEC_REG_CLOCK_0,
    BOARD_AUDIO_CODEC_REG_CLOCK_1,
    BOARD_AUDIO_CODEC_REG_CLOCK_2,
    BOARD_AUDIO_CODEC_REG_CLOCK_3,
    BOARD_AUDIO_CODEC_REG_CLOCK_4,
    BOARD_AUDIO_CODEC_REG_CLOCK_5,
    BOARD_AUDIO_CODEC_REG_CLOCK_6,
    BOARD_AUDIO_CODEC_REG_CLOCK_7,
    BOARD_AUDIO_CODEC_REG_DAC_STATE,
    BOARD_AUDIO_CODEC_REG_MUTE,
    BOARD_AUDIO_CODEC_REG_ROUTE_L,
    BOARD_AUDIO_CODEC_REG_ROUTE_R,
    BOARD_AUDIO_CODEC_REG_OUTPUT_POWER,
    BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_L,
    BOARD_AUDIO_CODEC_REG_DIGITAL_VOLUME_R,
    BOARD_AUDIO_CODEC_REG_ANALOG_VOLUME_L,
    BOARD_AUDIO_CODEC_REG_ANALOG_VOLUME_R,
    BOARD_AUDIO_CODEC_REG_STATUS,
    BOARD_AUDIO_CODEC_REG_STATUS_MASK,
    BOARD_AUDIO_CODEC_REG_FUNCTIONAL_MODE,
    BOARD_AUDIO_CODEC_REG_CHIP_ID,
    BOARD_AUDIO_CODEC_REG_COUNT_SENTINEL
} board_audio_codec_reg_id_t;

typedef struct
{
    uint32_t valid_mask;
    uint8_t expected[BOARD_AUDIO_CODEC_SNAPSHOT_REG_COUNT];
    uint8_t actual[BOARD_AUDIO_CODEC_SNAPSHOT_REG_COUNT];
    uint8_t read_ok;
    uint8_t i2c_error;
    uint8_t _pad[2];
} board_audio_codec_snapshot_t;

typedef enum
{
    BOARD_AUDIO_CODEC_RESET_UNSUPPORTED = 0U,
    BOARD_AUDIO_CODEC_RESET_HARDWARE = 1U,
    BOARD_AUDIO_CODEC_RESET_SOFTWARE = 2U
} board_audio_codec_reset_type_t;

typedef struct
{
    uint8_t supported;
    uint8_t reset_ok;
    uint8_t init_ok;
    board_audio_codec_reset_type_t reset_type;
    uint8_t reset_pin_used;
    uint32_t reset_low_duration_ms;
    uint32_t wait_ms;
    uint32_t i2c_errors;
    uint32_t write_failures;
    uint32_t readback_errors;
    uint8_t reinit_status;
    uint8_t reinit_failed_stage;
    uint8_t reinit_failed_page;
    uint8_t reinit_failed_reg;
    uint8_t reinit_expected;
    uint8_t reinit_actual;
    uint8_t reinit_mask;
    uint8_t _pad2;
} board_audio_codec_reset_diag_t;

void board_audio_codec_init(void);
void board_audio_init(void);
uint8_t board_audio_start_stream(int32_t *rx_buffer, int32_t *tx_buffer, uint32_t word_count);
uint8_t board_audio_is_rx_callback_handle(void *handle);
void board_audio_get_boot_diag(board_audio_boot_diag_t *out_diag);
void board_audio_get_runtime_diag(board_audio_runtime_diag_t *out_diag);
void board_audio_get_codec_post_test_snapshot(board_audio_codec_snapshot_t *out_snapshot);
/* Codec-only reset/reinitialisation; never touches STM32 clocks, SAI or DMA. */
uint8_t board_audio_codec_reset_and_reinit(board_audio_codec_reset_diag_t *out_diag);
uint8_t board_audio_is_tx_callback_handle(void *handle);
uint8_t board_audio_is_audio_dma_handle(void *handle);

void board_audio_unpack_input(const int32_t *AUDIO_RESTRICT rx,
                              StereoTrack *AUDIO_RESTRICT track_buf,
                              uint32_t frames,
                              float in_scale);

void board_audio_pack_output(int32_t *AUDIO_RESTRICT tx,
                             const float *AUDIO_RESTRICT main_l,
                             const float *AUDIO_RESTRICT main_r,
                             const float *AUDIO_RESTRICT cue_l,
                             const float *AUDIO_RESTRICT cue_r,
                             uint32_t frames,
                             float cue_gain_start,
                             float cue_gain_end);
