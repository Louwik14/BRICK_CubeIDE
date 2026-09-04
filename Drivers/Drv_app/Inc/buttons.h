#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

#include "buttons_ids.h"

typedef struct
{
    uint32_t capture_tick;
    uint32_t capture_ms;
    uint32_t ingress_serial;
    uint8_t shift_down;
    uint8_t track_select_armed;
} button_edge_context_t;

typedef struct
{
    uint8_t state;
    uint8_t prev_state;
    uint8_t pressed;
    uint8_t released;
    uint16_t debounce;
    button_edge_context_t pressed_context;
    button_edge_context_t released_context;
} button_state_t;

void buttons_init(void);
void buttons_update(uint32_t dt_ms);

uint8_t button_down(button_id_t btn);
uint8_t button_peek_pressed_event(button_id_t btn, button_edge_context_t *out_context);
uint8_t button_peek_released_event(button_id_t btn, button_edge_context_t *out_context);
uint8_t button_take_pressed_event(button_id_t btn, button_edge_context_t *out_context);
uint8_t button_take_released_event(button_id_t btn, button_edge_context_t *out_context);

#endif
