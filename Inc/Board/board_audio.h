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
    BOARD_AUDIO_BOOT_RX_DMA,
    BOARD_AUDIO_BOOT_SAI_SYNC
} board_audio_boot_error_t;

typedef enum
{
    AUDIO_INIT_NOT_STARTED = 0,
    AUDIO_INIT_CODEC,
    AUDIO_INIT_SAI_SYNC,
    AUDIO_INIT_READY,
    AUDIO_INIT_ERROR
} audio_init_state_t;

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

void board_audio_codec_init(void);
void board_audio_init(void);
uint8_t board_audio_start_stream(int32_t *rx_buffer,
                                 int32_t *tx_buffer,
                                 uint32_t word_count,
                                 volatile audio_init_state_t *init_state);
uint8_t board_audio_is_rx_callback_handle(void *handle);
void board_audio_get_boot_diag(board_audio_boot_diag_t *out_diag);

void board_audio_unpack_input(const int32_t *AUDIO_RESTRICT rx,
                              StereoTrack *AUDIO_RESTRICT track_buf,
                              uint32_t frames,
                              float in_scale);

void board_audio_pack_output(int32_t *AUDIO_RESTRICT tx,
                             const float *AUDIO_RESTRICT main_l,
                             const float *AUDIO_RESTRICT main_r,
                             uint32_t frames);
