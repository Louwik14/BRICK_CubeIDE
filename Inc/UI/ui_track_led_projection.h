#ifndef UI_TRACK_LED_PROJECTION_H
#define UI_TRACK_LED_PROJECTION_H

#include <stdint.h>

typedef enum
{
    UI_TRACK_LED_COLOR_NONE = 0,
    UI_TRACK_LED_COLOR_TOP_LEVEL_OFF,
    UI_TRACK_LED_COLOR_TOP_LEVEL_ACTIVE,
    UI_TRACK_LED_COLOR_GROUP_CHILD_INACTIVE,
    UI_TRACK_LED_COLOR_GROUP_CHILD_ACTIVE,
    UI_TRACK_LED_COLOR_FOCUS
} ui_track_led_color_t;

typedef struct
{
    uint8_t visible;
    ui_track_led_color_t color;
} ui_track_led_projection_t;

/* Projects live Track/GROUP authorities onto the hall used to select them. */
uint8_t ui_track_led_project_hall(uint8_t hall,
                                  ui_track_led_projection_t *out_projection);

#endif /* UI_TRACK_LED_PROJECTION_H */
