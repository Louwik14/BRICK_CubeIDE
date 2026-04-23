#ifndef UI_HALL_MODE_CONTRACT_H
#define UI_HALL_MODE_CONTRACT_H

#include <stdint.h>

#include "ui_core.h"

#define UI_HALL_MODE_TRIGGER_NONE 0xFFU
#define UI_HALL_MODE_TARGET_PAGE_NONE 0xFFU

uint8_t ui_hall_mode_get_trigger_hall(ui_hall_mode_t mode, uint8_t *out_hall);
uint8_t ui_hall_mode_get_target_page(ui_hall_mode_t mode, uint8_t *out_page);
const char *ui_hall_mode_get_base_label(ui_hall_mode_t mode);

#endif /* UI_HALL_MODE_CONTRACT_H */
