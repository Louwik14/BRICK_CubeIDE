#include "pages/ui_page_audio_rec.h"

#include "buttons.h"
#include "App/control_domain.h"
#include "drv_display.h"
#include "font.h"
#include "Storage/sample_capture.h"
#include "ui_event.h"
#include "ui_page_manager.h"

#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
#include "stm32h7xx_hal.h"
#endif
#include <math.h>
#include <stdio.h>

#define UI_AUDIO_REC_WAVE_X      0
#define UI_AUDIO_REC_WAVE_Y      17
#define UI_AUDIO_REC_WAVE_W      OLED_WIDTH
#define UI_AUDIO_REC_WAVE_H      26
#define UI_AUDIO_REC_METER_X     11U
#define UI_AUDIO_REC_METER_Y     49U
#define UI_AUDIO_REC_METER_W     49U
#define UI_AUDIO_REC_METER_H     8U
#define UI_AUDIO_REC_STATUS_X    64U
#define UI_REC_EDIT_OVERVIEW_X   0
#define UI_REC_EDIT_OVERVIEW_Y   45
#define UI_REC_EDIT_OVERVIEW_W   OLED_WIDTH
#define UI_REC_EDIT_OVERVIEW_H   13
#define UI_REC_EDIT_TITLE_Y      0U
#define UI_REC_EDIT_TITLE_GAP    2U

#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
static uint32_t g_ui_page_audio_rec_last_waveform_ms;
#endif
static uint8_t g_ui_rec_edit_assign_popup;
static uint32_t g_ui_audio_rec_smoothed_peak;

static const char *ui_page_audio_rec_arm_label(sample_capture_arm_t arm)
{
    switch(arm)
    {
        case SAMPLE_CAPTURE_ARM_OFF: return "OFF";
        case SAMPLE_CAPTURE_ARM_REC: return "REC";
        case SAMPLE_CAPTURE_ARM_TRIG: return "TRIG";
        default: return "?";
    }
}

static uint8_t ui_page_audio_rec_peak_to_meter(uint32_t peak_abs_pcm24,
                                               uint8_t width)
{
    if((peak_abs_pcm24 == 0U) || (width == 0U))
    {
        return 0U;
    }
    float dbfs = 20.0f * log10f((float)peak_abs_pcm24 / 8388607.0f);
    if(dbfs < -60.0f)
    {
        dbfs = -60.0f;
    }
    if(dbfs > 0.0f)
    {
        dbfs = 0.0f;
    }
    return (uint8_t)(((dbfs + 60.0f) * (float)width / 60.0f) + 0.5f);
}

static void ui_page_audio_rec_update_meter(uint32_t peak_abs_pcm24)
{
    if(peak_abs_pcm24 >= g_ui_audio_rec_smoothed_peak)
    {
        g_ui_audio_rec_smoothed_peak = peak_abs_pcm24;
        return;
    }
    uint32_t release = (g_ui_audio_rec_smoothed_peak - peak_abs_pcm24) >> 2U;
    if(release == 0U)
    {
        release = 1U;
    }
    g_ui_audio_rec_smoothed_peak -= release;
}

static void ui_page_audio_rec_draw_live_meter(const sample_capture_state_t *state)
{
    if(state == 0)
    {
        return;
    }
    const uint8_t inner_w = UI_AUDIO_REC_METER_W - 2U;
    const uint8_t fill_w = ui_page_audio_rec_peak_to_meter(
        g_ui_audio_rec_smoothed_peak, inner_w);
    drv_display_draw_text(0U, 50U, "IN");
    drv_display_draw_rect(UI_AUDIO_REC_METER_X, UI_AUDIO_REC_METER_Y,
                          UI_AUDIO_REC_METER_W, UI_AUDIO_REC_METER_H);
    if(fill_w != 0U)
    {
        drv_display_fill_rect(UI_AUDIO_REC_METER_X + 1U,
                              UI_AUDIO_REC_METER_Y + 1U,
                              fill_w, UI_AUDIO_REC_METER_H - 2U);
    }
    if(state->arm == SAMPLE_CAPTURE_ARM_TRIG)
    {
        const int16_t threshold_from_floor =
            (int16_t)state->threshold_dbfs + 60;
        const uint8_t marker_x = (uint8_t)(UI_AUDIO_REC_METER_X + 1U
            + ((uint16_t)threshold_from_floor * inner_w) / 60U);
        drv_display_draw_line(marker_x, UI_AUDIO_REC_METER_Y - 1U,
                              marker_x,
                              UI_AUDIO_REC_METER_Y + UI_AUDIO_REC_METER_H);
    }
}

static uint8_t ui_page_audio_rec_center_x(const char *label)
{
    const uint8_t width = drv_display_text_width(label);
    if(width >= OLED_WIDTH)
    {
        return 0U;
    }
    return (uint8_t)((OLED_WIDTH - width) / 2U);
}

static void ui_page_audio_rec_draw_label(uint8_t x, uint8_t y, const char *label, uint8_t inverted)
{
    if(label == 0)
    {
        return;
    }
    if(inverted != 0U)
    {
        const uint8_t w = (uint8_t)(drv_display_text_width(label) + 2U);
        const uint8_t h = (uint8_t)(drv_display_font_height() + 1U);
        const uint8_t rect_y = (y > 0U) ? (uint8_t)(y - 1U) : 0U;
        drv_display_fill_rect(x, rect_y, w, h);
        drv_display_draw_text_inverted((uint8_t)(x + 1U), y, label);
    }
    else
    {
        drv_display_draw_text(x, y, label);
    }
}

static uint16_t ui_page_audio_rec_vzoom_scale_q8(uint8_t vzoom)
{
    static const uint16_t k_scale_q8[] = {
        128U, 192U, 256U, 384U, 512U, 768U, 1024U, 1536U, 2048U
    };
    if(vzoom >= (uint8_t)(sizeof(k_scale_q8) / sizeof(k_scale_q8[0])))
    {
        vzoom = (uint8_t)((sizeof(k_scale_q8) / sizeof(k_scale_q8[0])) - 1U);
    }
    return k_scale_q8[vzoom];
}


/* Recorder page, take editor and rendering/input remain in their original sequence.
 * Private fragments share this translation unit to preserve UI state and call order. */

#include "AudioRec/ui_audio_rec_page.inc"

#include "AudioRec/ui_audio_rec_take_editor.inc"

#include "AudioRec/ui_audio_rec_render_input.inc"
