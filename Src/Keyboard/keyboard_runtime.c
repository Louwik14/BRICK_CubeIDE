/******************************************************************************
 * @file    keyboard_runtime.c
 * @brief   Orchestrateur principal du sous-système clavier.
 *
 * Ce module coordonne les différents blocs du clavier :
 * - initialisation générale
 * - tick runtime
 * - wrappers publics appelés par le reste de l’application
 * - gestion des changements de mode et de track
 *
 * Il ne contient plus la logique interne détaillée de l’arpégiateur,
 * du moteur de notes, des paramètres clavier ou des entrées hall.
 * Ces responsabilités sont déportées dans des modules dédiés.
 ******************************************************************************/

#include "Keyboard/keyboard_runtime.h"

#include "Keyboard/keyboard_engine.h"
#include "Keyboard/keyboard_params.h"
#include "Keyboard/keyboard_input.h"
#include "Seq/seq_edit.h"
#include "NoteFx/note_fx_pipeline.h"
#include "Track/control_music_output.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_runtime.h"
#include "Seq/seq_play_scheduler.h"

#include "ui_core.h"

#include <string.h>

#define KEYBOARD_RUNTIME_MIDI_CHANNEL_COUNT 16U
#define KEYBOARD_RUNTIME_MIDI_NOTE_COUNT 128U

static uint8_t g_keyboard_runtime_midi_held_count[KEYBOARD_RUNTIME_MIDI_CHANNEL_COUNT][KEYBOARD_RUNTIME_MIDI_NOTE_COUNT];
static uint8_t g_keyboard_runtime_midi_sustained_release[KEYBOARD_RUNTIME_MIDI_CHANNEL_COUNT][KEYBOARD_RUNTIME_MIDI_NOTE_COUNT];
static uint8_t g_keyboard_runtime_midi_sustain_down[KEYBOARD_RUNTIME_MIDI_CHANNEL_COUNT];
static uint8_t g_keyboard_runtime_timed_context_active;
static uint32_t g_keyboard_runtime_capture_tick;
static uint32_t g_keyboard_runtime_ingress_serial;

void keyboard_runtime_all_notes_off(void);

static void keyboard_runtime_reset_midi_state(void)
{
    memset(g_keyboard_runtime_midi_held_count, 0, sizeof(g_keyboard_runtime_midi_held_count));
    memset(g_keyboard_runtime_midi_sustained_release, 0, sizeof(g_keyboard_runtime_midi_sustained_release));
    memset(g_keyboard_runtime_midi_sustain_down, 0, sizeof(g_keyboard_runtime_midi_sustain_down));
}

static void keyboard_runtime_send_to_engine(const uint8_t *msg, size_t len)
{
    if (g_keyboard_runtime_timed_context_active != 0U)
    {
        keyboard_engine_midi_receive_timed(msg, len,
                                           g_keyboard_runtime_capture_tick,
                                           g_keyboard_runtime_ingress_serial);
    }
    else
    {
        keyboard_engine_midi_receive(msg, len);
    }
}

static void keyboard_runtime_send_note_off_to_engine(uint8_t channel_zero_based, uint8_t note)
{
    const uint8_t msg[3] = {
        (uint8_t)(0x80U | (channel_zero_based & 0x0FU)),
        (uint8_t)(note & 0x7FU),
        0U
    };
    keyboard_runtime_send_to_engine(msg, sizeof(msg));
}

static void keyboard_runtime_flush_sustained_notes(uint8_t channel_zero_based)
{
    for (uint8_t note = 0U; note < KEYBOARD_RUNTIME_MIDI_NOTE_COUNT; ++note)
    {
        if ((g_keyboard_runtime_midi_sustained_release[channel_zero_based][note] == 0U) ||
            (g_keyboard_runtime_midi_held_count[channel_zero_based][note] != 0U))
        {
            continue;
        }

        g_keyboard_runtime_midi_sustained_release[channel_zero_based][note] = 0U;
        keyboard_runtime_send_note_off_to_engine(channel_zero_based, note);
    }
}

static void keyboard_runtime_handle_note_on(const uint8_t *msg, size_t len)
{
    const uint8_t channel_zero_based = (uint8_t)(msg[0] & 0x0FU);
    const uint8_t note = (uint8_t)(msg[1] & 0x7FU);

    if ((len < 3U) || (note >= KEYBOARD_RUNTIME_MIDI_NOTE_COUNT))
    {
        return;
    }

    if (g_keyboard_runtime_midi_held_count[channel_zero_based][note] < 0xFFU)
    {
        g_keyboard_runtime_midi_held_count[channel_zero_based][note]++;
    }
    g_keyboard_runtime_midi_sustained_release[channel_zero_based][note] = 0U;

    keyboard_runtime_send_to_engine(msg, len);
}

static void keyboard_runtime_handle_note_off(const uint8_t *msg, size_t len)
{
    const uint8_t channel_zero_based = (uint8_t)(msg[0] & 0x0FU);
    const uint8_t note = (uint8_t)(msg[1] & 0x7FU);

    if ((len < 3U) || (note >= KEYBOARD_RUNTIME_MIDI_NOTE_COUNT))
    {
        return;
    }

    const uint8_t was_held = g_keyboard_runtime_midi_held_count[channel_zero_based][note];
    if (g_keyboard_runtime_midi_held_count[channel_zero_based][note] > 0U)
    {
        g_keyboard_runtime_midi_held_count[channel_zero_based][note]--;
    }

    if ((g_keyboard_runtime_midi_sustain_down[channel_zero_based] != 0U) && (was_held > 0U))
    {
        if (g_keyboard_runtime_midi_held_count[channel_zero_based][note] == 0U)
        {
            g_keyboard_runtime_midi_sustained_release[channel_zero_based][note] = 1U;
        }
        return;
    }

    keyboard_runtime_send_to_engine(msg, len);
}

static void keyboard_runtime_handle_all_notes_off(const uint8_t *msg, size_t len)
{
    if (len >= 2U)
    {
        const uint8_t channel_zero_based = (uint8_t)(msg[0] & 0x0FU);
        memset(g_keyboard_runtime_midi_held_count[channel_zero_based], 0, sizeof(g_keyboard_runtime_midi_held_count[channel_zero_based]));
        memset(g_keyboard_runtime_midi_sustained_release[channel_zero_based], 0, sizeof(g_keyboard_runtime_midi_sustained_release[channel_zero_based]));
        g_keyboard_runtime_midi_sustain_down[channel_zero_based] = 0U;
    }
    /* MIDI CC120/123 enters the priority NoteFx/audio-owned panic path.  It
     * must not compete with the ordinary transition queue. */
    keyboard_runtime_send_to_engine(msg, len);
}

void keyboard_runtime_init(void)
{
    keyboard_input_init();
    keyboard_params_init();
    note_fx_pipeline_init();
    keyboard_runtime_reset_midi_state();
    g_keyboard_runtime_timed_context_active = 0U;
    g_keyboard_runtime_capture_tick = 0U;
    g_keyboard_runtime_ingress_serial = 0U;
}

void keyboard_runtime_tick(void)
{
    seq_runtime_live_rec_drain_effective();
    ui_keyboard_app_tick(0U);

}

void keyboard_runtime_set_root(uint8_t root_index) { keyboard_params_set_root(root_index); }
void keyboard_runtime_set_scale(uint8_t scale_index) { keyboard_params_set_scale(scale_index); }
void keyboard_runtime_set_omnichord(bool enabled) { keyboard_params_set_omnichord(enabled); }
void keyboard_runtime_set_note_order(note_order_t order) { keyboard_params_set_note_order(order); }
void keyboard_runtime_set_chord_override(bool enabled) { keyboard_params_set_chord_override(enabled); }
void keyboard_runtime_set_mono_last(bool enabled)
{
    keyboard_params_set_mono_last(enabled);
    if (!enabled)
    {
        keyboard_engine_clear_state_silent();
    }
}


void keyboard_runtime_step_octave(int8_t delta)
{
    ui_keyboard_app_set_octave_shift((int8_t)(ui_keyboard_app_get_octave_shift() + delta));
}

void keyboard_runtime_process_midi(const uint8_t *msg, size_t len, seq_clock_src_t source)
{
    (void)source;

    if ((msg == NULL) || (len == 0U))
    {
        return;
    }

    const uint8_t status = msg[0];
    const uint8_t type = (uint8_t)(status & 0xF0U);
    const uint8_t channel = (uint8_t)(status & 0x0FU);

    switch (type)
    {
        case 0x80U:
            keyboard_runtime_handle_note_off(msg, len);
            break;

        case 0x90U:
            if (len >= 3U)
            {
                const uint8_t velocity = (uint8_t)(msg[2] & 0x7FU);
                if (velocity == 0U)
                {
                    keyboard_runtime_handle_note_off(msg, len);
                }
                else
                {
                    keyboard_runtime_handle_note_on(msg, len);
                }
            }
            break;

        case 0xB0U:
            if (len >= 3U)
            {
                const uint8_t cc = (uint8_t)(msg[1] & 0x7FU);
                const uint8_t value = (uint8_t)(msg[2] & 0x7FU);

                if (cc == 64U)
                {
                    if (value >= 64U)
                    {
                        g_keyboard_runtime_midi_sustain_down[channel] = 1U;
                    }
                    else
                    {
                        g_keyboard_runtime_midi_sustain_down[channel] = 0U;
                        keyboard_runtime_flush_sustained_notes(channel);
                    }
                }
                else if ((cc == 120U) || (cc == 123U))
                {
                    keyboard_runtime_handle_all_notes_off(msg, len);
                }
                else
                {
                    keyboard_runtime_send_to_engine(msg, len);
                }
            }
            break;

        case 0xE0U:
            /* No dedicated pitch-bend hook in the current keyboard runtime. */
            keyboard_runtime_send_to_engine(msg, len);
            break;

        default:
            keyboard_runtime_send_to_engine(msg, len);
            break;
    }
}

void keyboard_runtime_process_midi_timed(const uint8_t *msg, size_t len,
                                         seq_clock_src_t source,
                                         uint32_t capture_tick,
                                         uint32_t ingress_serial)
{
    g_keyboard_runtime_timed_context_active = 1U;
    g_keyboard_runtime_capture_tick = capture_tick;
    g_keyboard_runtime_ingress_serial = ingress_serial;
    keyboard_runtime_process_midi(msg, len, source);
    g_keyboard_runtime_timed_context_active = 0U;
}

void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity)
{
    keyboard_input_process_hall(hall_index, pressed, velocity);
}

void keyboard_runtime_process_hall_timed(uint8_t hall_index, bool pressed,
                                         uint8_t velocity, uint32_t capture_tick,
                                         uint32_t ingress_serial)
{
    keyboard_input_process_hall_timed(hall_index, pressed, velocity,
                                      capture_tick, ingress_serial);
}

void keyboard_runtime_all_notes_off(void)
{
    keyboard_runtime_reset_midi_state();
    ui_keyboard_app_all_notes_off();
    seq_edit_note_capture_reset();
    (void)control_music_output_panic_all(0U);
    note_fx_pipeline_panic();
    seq_play_scheduler_clear();
    keyboard_engine_clear_source_occurrences_silent();
}

void keyboard_runtime_sync_track_focus_context(void)
{
    const ui_hall_mode_t hall_mode = ui_get_hall_mode();
    if (hall_mode != UI_HALL_MODE_KEYBOARD)
    {
        return;
    }

    /*
     * Le changement de focus track est une action UI.
     * On ne doit pas envoyer de panic/runtime reset global ici,
     * sinon on coupe aussi des notes/séquences qui jouent encore.
     *
     * On purge uniquement l'état local clavier pour éviter que
     * des relâchements tardifs d'anciens halls pilotent la nouvelle track.
    */
}

void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode)
{
    if ((previous_mode == UI_HALL_MODE_KEYBOARD) && (new_mode != UI_HALL_MODE_KEYBOARD))
    {
        ui_keyboard_app_all_notes_off();
    }
}

uint8_t keyboard_runtime_get_root_index(void) { return keyboard_params_get_root_index(); }
uint8_t keyboard_runtime_get_scale_index(void) { return keyboard_params_get_scale_index(); }
bool keyboard_runtime_get_omnichord(void) { return keyboard_params_get_omnichord(); }
note_order_t keyboard_runtime_get_note_order(void) { return keyboard_params_get_note_order(); }
bool keyboard_runtime_get_chord_override(void) { return keyboard_params_get_chord_override(); }
bool keyboard_runtime_get_mono_last(void) { return keyboard_params_get_mono_last(); }

int8_t keyboard_runtime_get_octave_shift(void)
{
    return ui_keyboard_app_get_octave_shift();
}

void keyboard_runtime_get_active_chord_label(char *out, uint32_t out_len)
{
    ui_keyboard_app_format_active_chord_label(out, out_len);
}
