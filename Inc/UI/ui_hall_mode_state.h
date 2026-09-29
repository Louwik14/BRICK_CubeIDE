#ifndef UI_HALL_MODE_STATE_H
#define UI_HALL_MODE_STATE_H

#include "ui_core.h"

ui_hall_mode_t ui_get_hall_mode(void);
void ui_set_hall_mode(ui_hall_mode_t mode);
void ui_restore_hall_mode_before_macro(void);
ui_hall_mode_t ui_get_hall_mode_before_macro(void);

#endif /* UI_HALL_MODE_STATE_H */
