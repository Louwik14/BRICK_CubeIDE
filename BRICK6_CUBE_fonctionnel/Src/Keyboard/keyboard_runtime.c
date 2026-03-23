#include "Keyboard/keyboard_runtime.h"

#include "Keyboard/kbd_input_mapper.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "ui_core.h"

typedef struct
{
    uint8_t root_index;
    uint8_t scale_index;
    bool omnichord;
    note_order_t note_order;
    bool chord_override;
    ui_track_type_t sounding_type;
    bool sounding_active;
} keyboard_runtime_state_t;

static keyboard_runtime_state_t g_keyboard_runtime = {
    .root_index = 0U,
    .scale_index = (uint8_t)KBD_SCALE_MAJOR,
    .omnichord = false,
    .note_order = NOTE_ORDER_NATURAL,
    .chord_override = false,
    .sounding_type = UI_TRACK_TYPE_DX7,
    .sounding_active = false,
};

static bool keyboard_runtime_active_track_is_synth(void)
{
    return (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH);
}

static ui_track_type_t keyboard_runtime_get_active_synth_type(void)
{
    return ui_get_track_type(ui_get_active_track());
}

static void keyboard_runtime_note_on_sink(uint8_t note, uint8_t velocity)
{
    if (!keyboard_runtime_active_track_is_synth())
    {
        return;
    }

    const ui_track_type_t synth_type = keyboard_runtime_get_active_synth_type();
    g_keyboard_runtime.sounding_type = synth_type;
    g_keyboard_runtime.sounding_active = true;

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_on(note, velocity);
    }
    else
    {
        microdexed_synth_note_on(note, velocity);
    }
}

static void keyboard_runtime_note_off_sink(uint8_t note)
{
    const ui_track_type_t synth_type = g_keyboard_runtime.sounding_active ? g_keyboard_runtime.sounding_type : keyboard_runtime_get_active_synth_type();

    if (synth_type == UI_TRACK_TYPE_MONOB)
    {
        monob_synth_note_off(note);
    }
    else
    {
        microdexed_synth_note_off(note);
    }
}

static void keyboard_runtime_all_notes_off_sink(void)
{
    microdexed_synth_all_notes_off();
    monob_synth_all_notes_off();
    g_keyboard_runtime.sounding_active = false;
}

static void keyboard_runtime_apply_params(void)
{
    ui_keyboard_app_set_params((uint8_t)(60U + (g_keyboard_runtime.root_index % 12U)),
                               (kbd_scale_t)g_keyboard_runtime.scale_index,
                               g_keyboard_runtime.omnichord);
    ui_keyboard_app_set_note_order(g_keyboard_runtime.note_order);
    ui_keyboard_app_set_chord_override(g_keyboard_runtime.chord_override);
    kbd_input_mapper_set_omnichord_state(g_keyboard_runtime.omnichord);
}

void keyboard_runtime_init(void)
{
    const ui_keyboard_note_sink_t sink = {
        .note_on = keyboard_runtime_note_on_sink,
        .note_off = keyboard_runtime_note_off_sink,
        .all_notes_off = keyboard_runtime_all_notes_off_sink,
        .velocity = 100U,
    };

    ui_keyboard_app_init(&sink);
    kbd_input_mapper_init(g_keyboard_runtime.omnichord);
    keyboard_runtime_apply_params();
}

void keyboard_runtime_tick(void)
{
    ui_keyboard_app_tick(0U);
}

void keyboard_runtime_set_root(uint8_t root_index)
{
    g_keyboard_runtime.root_index = (uint8_t)(root_index % 12U);
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_scale(uint8_t scale_index)
{
    if (scale_index > (uint8_t)KBD_SCALE_CHROMATIC)
    {
        scale_index = (uint8_t)KBD_SCALE_CHROMATIC;
    }

    g_keyboard_runtime.scale_index = scale_index;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_omnichord(bool enabled)
{
    g_keyboard_runtime.omnichord = enabled;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_note_order(note_order_t order)
{
    g_keyboard_runtime.note_order = order;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_set_chord_override(bool enabled)
{
    g_keyboard_runtime.chord_override = enabled;
    keyboard_runtime_apply_params();
}

void keyboard_runtime_step_octave(int8_t delta)
{
    ui_keyboard_app_set_octave_shift((int8_t)(ui_keyboard_app_get_octave_shift() + delta));
}

void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity)
{
    ui_keyboard_app_set_velocity(velocity);
    kbd_input_mapper_process((uint8_t)(hall_index + 1U), pressed);
}

void keyboard_runtime_all_notes_off(void)
{
    ui_keyboard_app_all_notes_off();
    g_keyboard_runtime.sounding_active = false;
}

void keyboard_runtime_on_active_track_changed(void)
{
    keyboard_runtime_all_notes_off();
}

uint8_t keyboard_runtime_get_root_index(void)
{
    return g_keyboard_runtime.root_index;
}

uint8_t keyboard_runtime_get_scale_index(void)
{
    return g_keyboard_runtime.scale_index;
}

bool keyboard_runtime_get_omnichord(void)
{
    return g_keyboard_runtime.omnichord;
}

note_order_t keyboard_runtime_get_note_order(void)
{
    return g_keyboard_runtime.note_order;
}

bool keyboard_runtime_get_chord_override(void)
{
    return g_keyboard_runtime.chord_override;
}

int8_t keyboard_runtime_get_octave_shift(void)
{
    return ui_keyboard_app_get_octave_shift();
}
