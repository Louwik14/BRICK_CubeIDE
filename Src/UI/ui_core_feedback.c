#include "ui_core_feedback.h"

#include <stdio.h>
#include <string.h>

#define UI_CORE_FEEDBACK_DURATION_MS 1000U
#define UI_CORE_FEEDBACK_TEXT_MAX 16U

typedef struct
{
    char message[UI_CORE_FEEDBACK_TEXT_MAX];
    uint32_t until_ms;
} ui_core_feedback_state_t;

static ui_core_feedback_state_t g_ui_core_feedback = {
    .message = { 0 },
    .until_ms = 0U
};

void ui_core_feedback_init(void)
{
    g_ui_core_feedback.message[0] = '\0';
    g_ui_core_feedback.until_ms = 0U;
}

void ui_core_feedback_set(const char *message, uint32_t now_ms)
{
    if (message == 0)
    {
        ui_core_feedback_init();
        return;
    }

    (void)snprintf(g_ui_core_feedback.message, sizeof(g_ui_core_feedback.message), "%s", message);
    g_ui_core_feedback.until_ms = now_ms + UI_CORE_FEEDBACK_DURATION_MS;
}

uint8_t ui_core_feedback_try_get_for_track(uint8_t active_track,
                                           uint8_t track,
                                           uint32_t now_ms,
                                           char *out,
                                           uint32_t out_len)
{
    if ((out == 0) || (out_len == 0U))
    {
        return 0U;
    }

    if ((track != active_track)
        || (g_ui_core_feedback.message[0] == '\0')
        || ((int32_t)(g_ui_core_feedback.until_ms - now_ms) <= 0))
    {
        return 0U;
    }

    (void)snprintf(out, out_len, "%s", g_ui_core_feedback.message);
    return 1U;
}
