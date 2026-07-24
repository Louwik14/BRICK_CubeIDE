/* Temporary low-cost bring-up page. Remove with lowcost_button_test_config.h. */
#include "pages/ui_page_lowcost_button_test.h"

#include <stdio.h>

#include "buttons_ids.h"
#include "drv_display.h"
#include "lowcost_button_test_config.h"

#if LOWCOST_BUTTON_TEST_PAGE

static const char *const g_button_names[BTN_COUNT] = {
    [BTN_PARAM_1] = "BTN_PARAM_1",
    [BTN_PARAM_2] = "BTN_PARAM_2",
    [BTN_PARAM_3] = "BTN_PARAM_3",
    [BTN_PARAM_4] = "BTN_PARAM_4",
    [BTN_PARAM_5] = "BTN_PARAM_5",
    [BTN_PARAM_6] = "BTN_PARAM_6",
    [BTN_PARAM_7] = "BTN_PARAM_7",
    [BTN_TRACK] = "BTN_TRACK",
    [BTN_PLAY] = "BTN_PLAY",
    [BTN_REC] = "BTN_REC",
    [BTN_SHIFT] = "BTN_SHIFT",
    [BTN_TRANSPOSE_UP] = "BTN_TRANSPOSE_UP",
    [BTN_TRANSPOSE_DOWN] = "BTN_TRANSPOSE_DOWN",
    [BTN_COPY] = "BTN_COPY",
    [BTN_PASTE] = "BTN_PASTE",
    [BTN_SETTINGS] = "BTN_SETTINGS",
    [BTN_PAGE_1] = "BTN_PAGE_1",
    [BTN_PAGE_2] = "BTN_PAGE_2",
    [BTN_PAGE_3] = "BTN_PAGE_3",
    [BTN_PAGE_4] = "BTN_PAGE_4",
    [BTN_ENCODER_1_PUSH] = "BTN_ENCODER_1_PUSH",
    [BTN_ENCODER_2_PUSH] = "BTN_ENCODER_2_PUSH",
    [BTN_ENCODER_3_PUSH] = "BTN_ENCODER_3_PUSH",
    [BTN_ENCODER_4_PUSH] = "BTN_ENCODER_4_PUSH",
    [BTN_STEP_1] = "BTN_STEP_1",
    [BTN_STEP_2] = "BTN_STEP_2",
    [BTN_STEP_3] = "BTN_STEP_3",
    [BTN_STEP_4] = "BTN_STEP_4",
    [BTN_STEP_5] = "BTN_STEP_5",
    [BTN_STEP_6] = "BTN_STEP_6",
    [BTN_STEP_7] = "BTN_STEP_7",
    [BTN_STEP_8] = "BTN_STEP_8",
    [BTN_STEP_9] = "BTN_STEP_9",
    [BTN_STEP_10] = "BTN_STEP_10",
    [BTN_STEP_11] = "BTN_STEP_11",
    [BTN_STEP_12] = "BTN_STEP_12",
    [BTN_STEP_13] = "BTN_STEP_13",
    [BTN_STEP_14] = "BTN_STEP_14",
    [BTN_STEP_15] = "BTN_STEP_15",
    [BTN_STEP_16] = "BTN_STEP_16",
    [BTN_UNUSED_5] = "BTN_UNUSED_5",
    [BTN_UNUSED_6] = "BTN_UNUSED_6",
    [BTN_UNUSED_7] = "BTN_UNUSED_7",
    [BTN_UNUSED_8] = "BTN_UNUSED_8",
};

static button_id_t g_pressed_button = BTN_COUNT;

uint8_t ui_page_lowcost_button_test_capture_event(const ui_event_t *ev)
{
    if ((ev == NULL) || (ev->id >= (uint8_t)BTN_COUNT))
    {
        return 0U;
    }

    if (ev->type == UI_EVENT_BUTTON_PRESS)
    {
        g_pressed_button = (button_id_t)ev->id;
        return 1U;
    }

    if ((ev->type == UI_EVENT_BUTTON_RELEASE)
        && (g_pressed_button == (button_id_t)ev->id))
    {
        g_pressed_button = BTN_COUNT;
        return 1U;
    }

    return 0U;
}

static void ui_page_lowcost_button_test_enter(void)
{
    g_pressed_button = BTN_COUNT;
}

static void ui_page_lowcost_button_test_leave(void) {}

static void ui_page_lowcost_button_test_handle_event(const ui_event_t *ev)
{
    (void)ui_page_lowcost_button_test_capture_event(ev);
}

static void ui_page_lowcost_button_test_tick(void) {}

static void ui_page_lowcost_button_test_render(void)
{
    char line[32];

    drv_display_draw_text(0U, 0U, "LOWCOST BUTTON TEST");
    if (g_pressed_button == BTN_COUNT)
    {
        drv_display_draw_text(0U, 20U, "PRESS A BUTTON");
        return;
    }

    (void)snprintf(line, sizeof(line), "ID: %u", (unsigned)g_pressed_button);
    drv_display_draw_text(0U, 20U, line);
    drv_display_draw_text(0U, 34U, g_button_names[g_pressed_button]);
}

#else

static void ui_page_lowcost_button_test_noop(void) {}

uint8_t ui_page_lowcost_button_test_capture_event(const ui_event_t *ev)
{
    (void)ev;
    return 0U;
}

#endif

const ui_page_t g_ui_page_lowcost_button_test = {
#if LOWCOST_BUTTON_TEST_PAGE
    .enter = ui_page_lowcost_button_test_enter,
    .leave = ui_page_lowcost_button_test_leave,
    .handle_event = ui_page_lowcost_button_test_handle_event,
    .tick = ui_page_lowcost_button_test_tick,
    .render = ui_page_lowcost_button_test_render,
#else
    .enter = ui_page_lowcost_button_test_noop,
    .leave = ui_page_lowcost_button_test_noop,
    .handle_event = NULL,
    .tick = ui_page_lowcost_button_test_noop,
    .render = NULL,
#endif
};
