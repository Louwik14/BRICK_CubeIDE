#ifndef UI_PAGE_NAME_EDIT_H
#define UI_PAGE_NAME_EDIT_H

#include <stdint.h>

#include "ui_page.h"

#define UI_PAGE_NAME_EDIT_TEXT_MAX 32U

typedef enum
{
    UI_PAGE_NAME_EDIT_RESULT_CANCEL = 0,
    UI_PAGE_NAME_EDIT_RESULT_OK
} ui_page_name_edit_result_t;

typedef void (*ui_page_name_edit_done_fn)(ui_page_name_edit_result_t result,
                                          const char *name,
                                          void *user);

extern const ui_page_t g_ui_page_name_edit;

uint8_t ui_page_name_edit_open(uint8_t return_page,
                               const char *title,
                               const char *context,
                               const char *initial,
                               uint8_t max_len,
                               ui_page_name_edit_done_fn done,
                               void *user);
uint8_t ui_page_name_edit_is_open(void);
uint8_t ui_page_name_edit_handle_encoder(uint8_t encoder, int16_t delta);

#endif /* UI_PAGE_NAME_EDIT_H */
