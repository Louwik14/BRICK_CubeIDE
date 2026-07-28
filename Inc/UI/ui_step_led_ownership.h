#ifndef UI_STEP_LED_OWNERSHIP_H
#define UI_STEP_LED_OWNERSHIP_H

#include <stdint.h>

#include "ui_core.h"

uint8_t ui_step_led_ownership_page_needs_step_leds(uint8_t page_id);
uint8_t ui_step_led_ownership_hall_mode_needs_step_leds(ui_hall_mode_t mode);

#endif /* UI_STEP_LED_OWNERSHIP_H */
