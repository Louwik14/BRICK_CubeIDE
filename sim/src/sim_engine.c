#include "sim_engine.h"

#include <stdio.h>
#include <string.h>

#include "drv_display.h"
#include "pages/ui_page_name_edit.h"
#include "ui_page_manager.h"
#include "ui_renderer_oled.h"

static void sim_name_edit_done(ui_page_name_edit_result_t result,
                               const char *name, void *user)
{
    (void)result;
    (void)name;
    (void)user;
}

int sim_engine_render(sim_engine_t *engine)
{
    if ((engine == NULL) || (engine->initialized == 0))
    {
        return 0;
    }

    for (unsigned pass = 0U; pass < 64U; ++pass)
    {
        ui_renderer_oled_draw();
        if (ui_renderer_oled_is_rendering() == 0U)
        {
            engine->renderer_ready = 1;
            engine->error[0] = '\0';
            return 1;
        }
    }

    engine->renderer_ready = 0;
    (void)snprintf(engine->error, sizeof(engine->error),
                   "renderer did not stabilize after 64 passes");
    return 0;
}

int sim_engine_init(sim_engine_t *engine)
{
    if (engine == NULL)
    {
        return 0;
    }

    memset(engine, 0, sizeof(*engine));
    sim_engine_set_hal_tick(0U);
    drv_display_init();
    if (drv_display_get_state() != DRV_DISPLAY_STATE_READY)
    {
        (void)snprintf(engine->error, sizeof(engine->error),
                       "BRICK display driver initialization failed");
        return 0;
    }

    ui_page_manager_init();
    ui_page_manager_register(UI_PAGE_NAME_EDIT, &g_ui_page_name_edit);
    engine->initialized = 1;

    if (ui_page_name_edit_open(UI_PAGE_NAME_EDIT, "PATCH NAME", "SLOT 001",
                               "INIT PATCH", UI_PAGE_NAME_EDIT_TEXT_MAX,
                               sim_name_edit_done, NULL) == 0U)
    {
        engine->initialized = 0;
        (void)snprintf(engine->error, sizeof(engine->error),
                       "real BRICK name editor page rejected initialization");
        return 0;
    }

    if (!sim_engine_render(engine))
    {
        return 0;
    }

    return 1;
}

int sim_engine_advance(sim_engine_t *engine, uint32_t milliseconds)
{
    if ((engine == NULL) || (engine->initialized == 0))
    {
        return 0;
    }

    engine->time_ms += milliseconds;
    sim_engine_set_hal_tick((uint32_t)engine->time_ms);
    const ui_page_t *page = ui_page_get();
    if ((page != NULL) && (page->tick != NULL))
    {
        page->tick();
    }
    ui_renderer_oled_service_poll();
    return sim_engine_render(engine);
}

const uint8_t *sim_engine_framebuffer(const sim_engine_t *engine)
{
    return ((engine != NULL) && (engine->initialized != 0))
        ? drv_display_get_buffer() : NULL;
}

size_t sim_engine_framebuffer_size(void) { return SIM_FRAMEBUFFER_SIZE; }

uint8_t sim_engine_page_id(const sim_engine_t *engine)
{
    return ((engine != NULL) && (engine->initialized != 0))
        ? ui_page_get_id() : 0U;
}

const char *sim_engine_page_name(const sim_engine_t *engine)
{
    if ((engine == NULL) || (engine->initialized == 0)) return "NONE";
    return (ui_page_get_id() == UI_PAGE_NAME_EDIT) ? "NAME_EDIT" : "UNKNOWN";
}

