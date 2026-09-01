#pragma once

#include <stdint.h>

void metronome_control_init(void);
uint8_t metronome_control_get_level(void);
uint8_t metronome_control_set_level(uint8_t level);
