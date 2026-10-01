#pragma once

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
    audio_init_state_t state;
    board_audio_boot_error_t error;
    uint16_t avg_permille;
    uint8_t cpu_load_valid;
    uint8_t reserved;
} audio_boot_diag_snapshot_t;
