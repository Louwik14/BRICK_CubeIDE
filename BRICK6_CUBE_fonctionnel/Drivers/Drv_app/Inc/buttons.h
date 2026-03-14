#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

#include "buttons_ids.h"

typedef struct
{
    uint8_t state;
    uint8_t prev_state;
    uint8_t pressed;
    uint8_t released;
    uint16_t debounce;
} button_state_t;

void buttons_init(void);
void buttons_update(uint32_t dt_ms);

uint8_t button_pressed(button_id_t btn);
uint8_t button_released(button_id_t btn);
uint8_t button_down(button_id_t btn);

#endif
