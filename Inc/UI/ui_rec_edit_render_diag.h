#pragma once

#include <stdint.h>
#include "stm32h7xx.h"

typedef enum
{
    UI_REC_EDIT_RENDER_MAIN = 0,
    UI_REC_EDIT_RENDER_OVERVIEW,
    UI_REC_EDIT_RENDER_REGION_COUNT
} ui_rec_edit_render_region_t;

typedef struct
{
    volatile uint32_t page_render_max_cycles;
    volatile uint32_t page_render_max_page_id;
    volatile uint32_t rec_edit_request_max_cycles[UI_REC_EDIT_RENDER_REGION_COUNT];
    volatile uint32_t rec_edit_columns_max_cycles[UI_REC_EDIT_RENDER_REGION_COUNT];
} ui_rec_edit_render_diag_t;

extern volatile ui_rec_edit_render_diag_t g_ui_rec_edit_render_diag;
extern volatile uint8_t g_ui_rec_edit_render_diag_reset_requested;

static inline void ui_rec_edit_render_diag_note_request(
    ui_rec_edit_render_region_t region, uint32_t started)
{
    const uint32_t elapsed = DWT->CYCCNT - started;
    if(elapsed > g_ui_rec_edit_render_diag.rec_edit_request_max_cycles[region])
        g_ui_rec_edit_render_diag.rec_edit_request_max_cycles[region] = elapsed;
}

static inline void ui_rec_edit_render_diag_note_columns(
    ui_rec_edit_render_region_t region, uint32_t started)
{
    const uint32_t elapsed = DWT->CYCCNT - started;
    if(elapsed > g_ui_rec_edit_render_diag.rec_edit_columns_max_cycles[region])
        g_ui_rec_edit_render_diag.rec_edit_columns_max_cycles[region] = elapsed;
}
