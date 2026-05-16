#ifndef UI_PAGE_AUDIO_REC_H
#define UI_PAGE_AUDIO_REC_H

#include <stdint.h>

#include "ui_page.h"

extern const ui_page_t g_ui_page_audio_rec;
extern const ui_page_t g_ui_page_rec_edit;

uint8_t ui_page_audio_rec_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t ui_page_audio_rec_is_open(void);

#endif /* UI_PAGE_AUDIO_REC_H */
