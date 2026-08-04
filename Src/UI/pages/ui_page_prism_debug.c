#include "pages/ui_page_prism_debug.h"

#include "Core/prism_debug_boot.h"

static void prism_debug_page_enter(void) {}
static void prism_debug_page_leave(void) {}
static void prism_debug_page_event(const ui_event_t *event)
{
    (void)prism_debug_boot_handle_event(event);
}
static void prism_debug_page_tick(void) {}
static void prism_debug_page_render(void)
{
    prism_debug_boot_render();
}

const ui_page_t g_ui_page_prism_debug = {
    .enter = prism_debug_page_enter,
    .leave = prism_debug_page_leave,
    .handle_event = prism_debug_page_event,
    .tick = prism_debug_page_tick,
    .render = prism_debug_page_render,
};
