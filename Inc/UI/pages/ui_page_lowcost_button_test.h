#ifndef UI_PAGE_LOWCOST_BUTTON_TEST_H
#define UI_PAGE_LOWCOST_BUTTON_TEST_H

#include "ui_page.h"

extern const ui_page_t g_ui_page_lowcost_button_test;

/* Captures every button event before normal navigation/shortcut consumers. */
uint8_t ui_page_lowcost_button_test_capture_event(const ui_event_t *ev);

#endif /* UI_PAGE_LOWCOST_BUTTON_TEST_H */
