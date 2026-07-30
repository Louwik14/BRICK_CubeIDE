#ifndef DIAGNOSTIC_INPUT_H
#define DIAGNOSTIC_INPUT_H

#include <stdint.h>

#include "Core/brick_build_config.h"

#if BRICK_TEST_BUILD

void diagnostic_input_init(void);
uint8_t diagnostic_input_button(uint8_t button, uint8_t pressed);
uint8_t diagnostic_input_encoder(uint8_t encoder, int16_t delta);
uint8_t diagnostic_input_key(uint8_t key, uint8_t pressed, uint8_t velocity);
void diagnostic_input_release_all(void);

#endif

#endif
