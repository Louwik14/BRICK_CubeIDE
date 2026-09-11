#pragma once

#include <stdint.h>

typedef enum
{
    BOARD_AUDIO_ANALOG_INPUT_LINE = 0,
    BOARD_AUDIO_ANALOG_INPUT_MIC
} board_audio_analog_input_mode_t;

uint8_t board_audio_set_analog_input_mode(board_audio_analog_input_mode_t mode);
