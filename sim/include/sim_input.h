#ifndef SIM_INPUT_H
#define SIM_INPUT_H

#include <stdint.h>
#include "buttons_ids.h"

void sim_input_reset(void);
int sim_input_set_button(button_id_t button, uint8_t down);
int sim_input_add_encoder(uint8_t encoder, int16_t delta);
uint8_t sim_input_button_state(button_id_t button);
uint64_t sim_input_button_mask(void);
void sim_input_release_all(void);

#endif

