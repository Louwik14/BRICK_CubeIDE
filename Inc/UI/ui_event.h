#ifndef UI_EVENT_H
#define UI_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    UI_EVENT_NONE,

    UI_EVENT_ENCODER,
    UI_EVENT_BUTTON_PRESS,
    UI_EVENT_BUTTON_RELEASE,
    UI_EVENT_HALL_PRESS,
    UI_EVENT_HALL_RELEASE,
    UI_EVENT_KEYBOARD_SHORTCUT

} ui_event_type_t;

typedef struct
{
    ui_event_type_t type;
    uint8_t id;
    int16_t value;
    uint32_t capture_tick;
    uint32_t capture_ms;
    uint32_t ingress_serial;
    uint8_t shift_down;
    uint8_t track_select_armed;
    uint8_t hall_mode;
    uint8_t context_track;
} ui_event_t;

void ui_event_from_inputs(void);
bool ui_event_push_hall(uint8_t hall, uint8_t pressed);
bool ui_event_push_hall_context(uint8_t hall, uint8_t pressed,
                                uint32_t capture_tick, uint32_t capture_ms,
                                uint32_t ingress_serial, uint8_t shift_down,
                                uint8_t track_select_armed, uint8_t hall_mode,
                                uint8_t context_track);
bool ui_event_push_encoder(uint8_t encoder, int8_t direction,
                           uint32_t capture_tick, uint32_t ingress_serial,
                           uint8_t shift_down, uint8_t context_track);
bool ui_event_push_keyboard_shortcut(uint8_t shortcut,
                                     uint32_t capture_tick,
                                     uint32_t capture_ms,
                                     uint32_t ingress_serial,
                                     uint8_t shift_down,
                                     uint8_t context_track);
bool ui_event_pop(ui_event_t *ev);
uint32_t ui_event_pending_count(void);
uint32_t ui_event_drop_count(void);

#endif /* UI_EVENT_H */
