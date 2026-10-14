#include "sim_engine.h"

#include <stdio.h>
#include <string.h>

#include "drv_display.h"
#include "buttons.h"
#include "buttons_ids.h"
#include "encoders.h"
#include "sim_input.h"
#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_renderer_oled.h"
#include "ui_template_page.h"
#include "Seq/seq_runtime.h"
#include "Param/param_registry.h"
#include "ui_param.h"
#include "ui_track_catalog.h"
#include "Track/track_runtime.h"
#include "sim_assets.h"

static const char *const g_button_names[BTN_COUNT] = {
    "PARAM_1", "PARAM_2", "PARAM_3", "PARAM_4", "PARAM_5", "PARAM_6", "PARAM_7", "TRACK",
    "PLAY", "REC", "SHIFT", "TRANSPOSE_UP", "TRANSPOSE_DOWN", "COPY", "PASTE", "SETTINGS",
    "PAGE_1", "PAGE_2", "PAGE_3", "PAGE_4", "ENCODER_1_PUSH", "ENCODER_2_PUSH",
    "ENCODER_3_PUSH", "ENCODER_4_PUSH", "STEP_1", "STEP_2", "STEP_3", "STEP_4", "STEP_5",
    "STEP_6", "STEP_7", "STEP_8", "STEP_9", "STEP_10", "STEP_11", "STEP_12", "STEP_13",
    "STEP_14", "STEP_15", "STEP_16", "UNUSED_5", "UNUSED_6", "UNUSED_7", "UNUSED_8"
};
static const char *const g_encoder_names[SIM_ENCODER_COUNT] = { "PAGE", "A", "B", "C" };
static const char *const g_page_names[UI_PAGE_COUNT] = {
    "NONE", "NONE", "CALIBRATION", "USER_CALIBRATION", "ENV", "CFG", "REC_CFG", "TONE",
    "MOD", "KEYBOARD", "MIDI_FX", "SEQ", "MIX", "PLAY", "NONE", "AUDIO_FX", "AUDIO_REC",
    "REC_EDIT", "PATCH_ASSIGN", "NAME_EDIT", "SETTINGS", "THEME_BROWSER"
};

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

    sim_input_reset();
    sim_assets_init();
    buttons_init();
    encoders_init();
    param_registry_init();
    ui_core_init();
    engine->initialized = 1;
    ui_page_set(UI_PAGE_TEMPLATE_CFG);

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

    for (uint32_t elapsed = 0U; elapsed < milliseconds; ++elapsed)
    {
        ++engine->time_ms;
        sim_engine_set_hal_tick((uint32_t)engine->time_ms);
        buttons_update(1U);
        encoders_update(1U);
        ui_core_service_track_selection_inputs();
        ui_core_tick();
        ui_renderer_oled_service_poll();
    }
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
    const uint8_t id = ui_page_get_id();
    return (id < UI_PAGE_COUNT) ? g_page_names[id] : "UNKNOWN";
}

int sim_engine_button_from_name(const char *name, unsigned *button)
{
    if ((name == NULL) || (button == NULL)) return 0;
    const char *plain = (strncmp(name, "BTN_", 4U) == 0) ? name + 4 : name;
    for (unsigned i = 0U; i < BTN_COUNT; ++i) if (strcmp(plain, g_button_names[i]) == 0) { *button = i; return 1; }
    return 0;
}
const char *sim_engine_button_name(unsigned button) { return button < BTN_COUNT ? g_button_names[button] : NULL; }
int sim_engine_encoder_from_name(const char *name, unsigned *encoder)
{
    if ((name == NULL) || (encoder == NULL)) return 0;
    const char *plain = (strncmp(name, "ENC_", 4U) == 0) ? name + 4 : name;
    for (unsigned i = 0U; i < SIM_ENCODER_COUNT; ++i) if (strcmp(plain, g_encoder_names[i]) == 0) { *encoder = i; return 1; }
    return 0;
}
const char *sim_engine_encoder_name(unsigned encoder) { return encoder < SIM_ENCODER_COUNT ? g_encoder_names[encoder] : NULL; }
int sim_engine_press(sim_engine_t *engine, unsigned button)
{
    return button < BTN_COUNT && sim_input_set_button((button_id_t)button, 1U) && sim_engine_advance(engine, 11U);
}
int sim_engine_release(sim_engine_t *engine, unsigned button)
{
    return button < BTN_COUNT && sim_input_set_button((button_id_t)button, 0U) && sim_engine_advance(engine, 11U);
}
int sim_engine_tap(sim_engine_t *engine, unsigned button, uint32_t down_ms)
{
    if (!sim_engine_press(engine, button)) return 0;
    if ((down_ms > 0U) && !sim_engine_advance(engine, down_ms)) return 0;
    return sim_engine_release(engine, button);
}
int sim_engine_combo(sim_engine_t *engine, const unsigned *buttons, size_t count, uint32_t down_ms)
{
    if ((buttons == NULL) || (count == 0U)) return 0;
    for (size_t i = 0U; i < count; ++i) if (buttons[i] >= BTN_COUNT || !sim_input_set_button((button_id_t)buttons[i], 1U)) return 0;
    if (!sim_engine_advance(engine, 11U + down_ms)) return 0;
    for (size_t i = 0U; i < count; ++i) (void)sim_input_set_button((button_id_t)buttons[i], 0U);
    return sim_engine_advance(engine, 11U);
}
int sim_engine_rotate(sim_engine_t *engine, unsigned encoder, int delta)
{
    if (encoder >= SIM_ENCODER_COUNT || delta < -32768 || delta > 32767 || !sim_input_add_encoder((uint8_t)encoder, (int16_t)delta)) return 0;
    return sim_engine_advance(engine, (uint32_t)(delta < 0 ? -delta : delta) + 1U);
}
int sim_engine_select_ensemble(sim_engine_t *engine, sim_ensemble_t ensemble)
{
    static const unsigned direct_buttons[] = {
        BTN_COUNT, BTN_PARAM_1, BTN_PARAM_2, BTN_PARAM_3,
        BTN_PARAM_4, BTN_PARAM_5, BTN_COUNT, BTN_COUNT
    };
    static const uint8_t target_pages[] = {
        UI_PAGE_TEMPLATE_CFG, UI_PAGE_TEMPLATE_ENV, UI_PAGE_TEMPLATE_TONE,
        UI_PAGE_TEMPLATE_MOD, UI_PAGE_TEMPLATE_MIX, UI_PAGE_TEMPLATE_PLAY,
        UI_PAGE_MIDI_FX, UI_PAGE_AUDIO_FX
    };
    if (engine == NULL || ensemble >= SIM_ENSEMBLE_COUNT) return 0;
    if (ensemble >= SIM_ENSEMBLE_ENV && ensemble <= SIM_ENSEMBLE_PLAY)
        return sim_engine_tap(engine, direct_buttons[ensemble], 40U);
    if (ensemble == SIM_ENSEMBLE_CFG)
    {
        const unsigned step = (unsigned)BTN_STEP_1 + sim_engine_active_track(engine);
        return step <= BTN_STEP_16
            && sim_engine_press(engine, BTN_TRACK)
            && sim_engine_tap(engine, step, 40U)
            && sim_engine_advance(engine, 80U)
            && sim_engine_tap(engine, step, 40U)
            && sim_engine_release(engine, BTN_TRACK);
    }
    for (unsigned attempt = 0U; attempt < 3U; ++attempt)
    {
        if (sim_engine_page_id(engine) == target_pages[ensemble]) return 1;
        if (!sim_engine_press(engine, BTN_SHIFT)
                || !sim_engine_tap(engine, BTN_STEP_7, 40U)
                || !sim_engine_release(engine, BTN_SHIFT)) return 0;
    }
    return sim_engine_page_id(engine) == target_pages[ensemble];
}
void sim_engine_release_all(sim_engine_t *engine) { sim_input_release_all(); if (engine != NULL) (void)sim_engine_advance(engine, 11U); }
uint64_t sim_engine_held_mask(void) { return sim_input_button_mask(); }
uint8_t sim_engine_active_track(const sim_engine_t *engine) { return (engine && engine->initialized) ? ui_get_active_track() : 0U; }
uint8_t sim_engine_transport_running(const sim_engine_t *engine) { return (engine && engine->initialized) ? seq_runtime_is_running() : 0U; }
uint32_t sim_engine_tempo_milli(const sim_engine_t *engine) { return (engine && engine->initialized) ? seq_runtime_get_tempo_bpm_milli() : 0U; }
void sim_engine_track_snapshot(const sim_engine_t *engine, sim_track_snapshot_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (engine == NULL || engine->initialized == 0) return;
    const uint8_t track = ui_get_active_track();
    const track_family_t family = ui_get_track_family(track);
    const track_type_t type = ui_get_track_type(track);
    track_runtime_descriptor_t descriptor;
    if (!track_runtime_get_descriptor(track, &descriptor)) return;
    out->available = 1U;
    out->family_id = (uint8_t)family;
    out->type_id = (uint8_t)type;
    out->engine_id = (uint8_t)descriptor.engine;
    out->family = ui_track_catalog_family_display_name(family);
    out->type = ui_track_catalog_type_display_name(family, type);
}
void sim_engine_parameter_snapshot(const sim_engine_t *engine,
                                   sim_parameter_snapshot_t out[SIM_ENCODER_COUNT])
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out) * SIM_ENCODER_COUNT);
    if (engine == NULL || engine->initialized == 0) return;
    ui_param_encoder_context_t context;
    ui_param_capture_encoder_context(&context);
    if (context.valid == 0U) return;
    for (uint8_t i = 0U; i < SIM_ENCODER_COUNT; ++i)
    {
        const param_id_t id = context.bank.params[i];
        if (id >= PARAM_COUNT) continue;
        out[i].available = 1U;
        out[i].id = (uint16_t)id;
        out[i].name = param_registry[id].name;
        out[i].value = ui_param_get_active_track_display_value(id, context.active_track);
    }
}
int sim_engine_subpage(const sim_engine_t *engine, uint8_t *subpage)
{
    if (!engine || !engine->initialized || !subpage) return 0;
    const uint8_t id = ui_page_get_id();
    if (id < UI_PAGE_TEMPLATE_ENV || id > UI_PAGE_TEMPLATE_PLAY || id == UI_PAGE_MIDI_FX || id == UI_PAGE_AUDIO_FX) return 0;
    const ui_page_t *page = ui_page_get();
    if (!page || !page->context) return 0;
    *subpage = ((const ui_template_page_state_t *)page->context)->active_subpage;
    return 1;
}
