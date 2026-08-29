#ifndef UI_PAGE_TEMPLATE_PLAY_H
#define UI_PAGE_TEMPLATE_PLAY_H

#include <stdint.h>

#include "param_registry.h"
#include "ui_page.h"

typedef struct
{
    uint8_t owner_track;
    uint8_t member_index;
    uint8_t target_track;
    param_id_t base_param;
} ui_page_template_play_context_t;

void ui_page_template_play_register_families(void);
void ui_page_template_play_open_primary(void);
void ui_page_template_play_toggle_subset(void);
uint8_t ui_page_template_play_resolve_context(param_id_t param,
                                              uint8_t active_track,
                                              ui_page_template_play_context_t *out_context);
uint8_t ui_page_template_play_resolve_param_track(param_id_t param, uint8_t active_track, uint8_t *out_track);
extern const ui_page_t g_ui_page_template_play;

#endif /* UI_PAGE_TEMPLATE_PLAY_H */
