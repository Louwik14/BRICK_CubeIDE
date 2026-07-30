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
} board_audio_boot_diag_t;

void board_audio_codec_init(void);
void board_audio_init(void);
uint8_t board_audio_start_stream(int32_t *rx_buffer, int32_t *tx_buffer, uint32_t word_count);
uint8_t board_audio_is_rx_callback_handle(void *handle);
void board_audio_get_boot_diag(board_audio_boot_diag_t *out_diag);

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
