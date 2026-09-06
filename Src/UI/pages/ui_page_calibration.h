#ifndef UI_PAGE_CALIBRATION_H
#define UI_PAGE_CALIBRATION_H

#include "ui_page.h"

extern const ui_page_t g_ui_page_calibration;
extern const ui_page_t g_ui_page_user_calibration;

void ui_page_calibration_open(uint8_t return_page_id);
void ui_page_user_calibration_open(uint8_t return_page_id);
void ui_page_calibration_process_presentation(uint8_t deadline_due);
uint8_t ui_page_calibration_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms);

#endif /* UI_PAGE_CALIBRATION_H */
