#ifndef UI_BOOT_LOADING_H
#define UI_BOOT_LOADING_H

#include <stdint.h>

void ui_boot_loading_begin(void);
void ui_boot_loading_service(void);
uint8_t ui_boot_loading_is_active(void);
uint8_t ui_boot_loading_restore_pending(void);
void ui_boot_loading_note_restore_started(uint8_t accepted);
void ui_boot_loading_advance_animation(void);
void ui_boot_loading_render(void);
void ui_boot_loading_discard_inputs(void);

#endif /* UI_BOOT_LOADING_H */
