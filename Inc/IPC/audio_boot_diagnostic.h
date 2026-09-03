#pragma once

#include <stdint.h>

typedef enum
{
    BOARD_AUDIO_BOOT_OK = 0,
    BOARD_AUDIO_BOOT_BAD_ARGUMENT = 1,
    BOARD_AUDIO_BOOT_CODEC_NOT_FOUND = 2,
    BOARD_AUDIO_BOOT_CODEC_RESET = 3,
    BOARD_AUDIO_BOOT_I2C = 4,
    BOARD_AUDIO_BOOT_VERIFY = 5,
    BOARD_AUDIO_BOOT_READY_TIMEOUT = 6,
    BOARD_AUDIO_BOOT_CLOCK = 7,
    BOARD_AUDIO_BOOT_CODEC_MUTED = 8,
    BOARD_AUDIO_BOOT_DAC_ROUTE = 9,
    BOARD_AUDIO_BOOT_OUTPUT_ROUTE = 10,
    BOARD_AUDIO_BOOT_OUTPUT_POWER = 11,
    BOARD_AUDIO_BOOT_VOLUME = 12,
    BOARD_AUDIO_BOOT_TX_DMA = 13,
    BOARD_AUDIO_BOOT_RX_DMA = 14,
    BOARD_AUDIO_BOOT_SAI_SYNC = 15
} board_audio_boot_error_t;

typedef enum
{
    AUDIO_INIT_NOT_STARTED = 0,
    AUDIO_INIT_CODEC = 1,
    AUDIO_INIT_SAI_SYNC = 2,
    AUDIO_INIT_READY = 3,
    AUDIO_INIT_ERROR = 4
} audio_init_state_t;

typedef struct
{
    uint8_t state;
    uint8_t error;
    uint16_t avg_permille;
    uint8_t cpu_load_valid;
    uint8_t reserved;
} audio_boot_diag_snapshot_t;

_Static_assert(sizeof(audio_boot_diag_snapshot_t) == 6U,
               "Boot diagnostic snapshot ABI changed");
