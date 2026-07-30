#ifndef UI_PAGE_PATCH_ASSIGN_H
#define UI_PAGE_PATCH_ASSIGN_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_page.h"

extern const ui_page_t g_ui_page_patch_assign;

void ui_page_patch_assign_open(uint8_t target_track, ui_hall_mode_t previous_hall_mode);
void ui_page_patch_assign_close(void);
uint8_t ui_page_patch_assign_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t ui_page_patch_assign_is_open(void);
uint8_t ui_page_patch_assign_get_target_hall_led(uint8_t hall, uint8_t *out_on);

#endif /* UI_PAGE_PATCH_ASSIGN_H */
