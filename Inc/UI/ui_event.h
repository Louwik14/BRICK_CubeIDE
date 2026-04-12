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
    UI_EVENT_HALL_RELEASE

} ui_event_type_t;

typedef struct
{
    ui_event_type_t type;
    uint8_t id;
    int16_t value;

} ui_event_t;

void ui_event_from_inputs(void);
bool ui_event_pop(ui_event_t *ev);

#endif /* UI_EVENT_H */
