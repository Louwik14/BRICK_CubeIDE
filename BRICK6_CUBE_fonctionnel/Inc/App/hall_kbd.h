#ifndef APP_HALL_KBD_H
#define APP_HALL_KBD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HALL_KBD_KEY_COUNT 16U

void hall_kbd_init(void);
void hall_kbd_poll(void);

uint8_t hall_kbd_is_pressed(uint8_t key);
uint8_t hall_kbd_get_velocity(uint8_t key);
uint8_t hall_kbd_get_value(uint8_t key);

uint32_t hall_kbd_get_scan_overrun_count(void);
uint32_t hall_kbd_get_event_overflow_count(void);
uint32_t hall_kbd_get_isr_max_cycles(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_HALL_KBD_H */
