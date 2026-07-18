#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(BRICK6_VARIANT_LOWCOST)
#define BOARD_LED_TRANSPORT_COUNT 21U
#else
#define BOARD_LED_TRANSPORT_COUNT 25U
#endif

void board_led_transport_init(void);
bool board_led_transport_busy(void);
void board_led_transport_send(const uint8_t *rgb, uint32_t count);
uint8_t board_led_transport_handle_pwm_callback(void *handle);
