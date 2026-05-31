/******************************************************************************
 * @file    ui_keyboard_app.c
 * @brief   Logique musicale de l’interface clavier.
 *
 * Ce module gère l’état musical du clavier côté application :
 * - notes actives et accords actifs
 * - mode normal / omnichord
 * - construction des notes à jouer selon gamme, root et ordre
 * - application de l’octave shift et de la quantification de gamme
 * - mise à jour différentielle des notes sonnées via un sink externe
 *
 * Il ne gère ni le scan matériel, ni le routage global de l’application,
 * ni la synthèse directement. Il produit un état musical cohérent à partir
 * des actions clavier et l’envoie vers la couche de sortie.
 ******************************************************************************/

#include "Keyboard/ui_keyboard_app.h"

#include <stdio.h>
#include <string.h>

#include "Keyboard/kbd_chords_dict.h"

#define KBD_MAX_VOICING_NOTES 12U

typedef struct
{
    bool omnichord;
    uint8_t ui_root_midi;
    kbd_scale_t ui_scale;
    note_order_t note_order;
    bool chord_override;
    int8_t octave_shift;
    uint8_t chord_mask;
    uint16_t note_mask;
    uint8_t root_stack[8];
    uint8_t root_stack_count;
    ui_keyboard_active_chord_t active;
    uint8_t sounding[UI_KEYBOARD_MAX_ACTIVE_NOTES];
    uint8_t sounding_count;
    ui_keyboard_note_sink_t sink;
    ui_keyboard_chord_cb_t observer;
} kbd_state_t;

static kbd_state_t g_keyboard_state;

static inline void kbd_sink_note_on(uint8_t note)
{
    if (g_keyboard_state.sink.note_on != NULL)
    {
        g_keyboard_state.sink.note_on(note, g_keyboard_state.sink.velocity);
    }
}

static inline void kbd_sink_note_off(uint8_t note)
{
    if (g_keyboard_state.sink.note_off != NULL)
    {
        g_keyboard_state.sink.note_off(note);
    }
}

static void kbd_add_note_unique(uint8_t *notes, uint8_t *count, uint8_t note)
{
    if ((notes == NULL) || (count == NULL) || (*count >= UI_KEYBOARD_MAX_ACTIVE_NOTES))
    {
        return;
    }

    for (uint8_t i = 0U; i < *count; ++i)
    {
        if (notes[i] == note)
        {
            return;
        }
    }

    notes[*count] = note;
    (*count)++;
}

static void kbd_root_stack_clear(void)
{
    g_keyboard_state.root_stack_count = 0U;
}

static void kbd_root_stack_remove(uint8_t slot)
{
    slot &= 7U;
    for (uint8_t i = 0U; i < g_keyboard_state.root_stack_count; ++i)
    {
        if (g_keyboard_state.root_stack[i] != slot)
        {
            continue;
        }

        for (uint8_t j = i; (uint8_t)(j + 1U) < g_keyboard_state.root_stack_count; ++j)
        {
            g_keyboard_state.root_stack[j] = g_keyboard_state.root_stack[j + 1U];
        }
        g_keyboard_state.root_stack_count--;
        return;
    }
}

static void kbd_root_stack_press(uint8_t slot)
{
    slot &= 7U;
    kbd_root_stack_remove(slot);
    if (g_keyboard_state.root_stack_count >= (uint8_t)(sizeof(g_keyboard_state.root_stack) / sizeof(g_keyboard_state.root_stack[0])))
    {
        g_keyboard_state.root_stack_count = (uint8_t)((sizeof(g_keyboard_state.root_stack) / sizeof(g_keyboard_state.root_stack[0])) - 1U);
    }
    g_keyboard_state.root_stack[g_keyboard_state.root_stack_count] = slot;
    g_keyboard_state.root_stack_count++;
}

static bool kbd_root_stack_active_slot(uint8_t *out_slot)
{
    while (g_keyboard_state.root_stack_count > 0U)
    {
        const uint8_t slot = g_keyboard_state.root_stack[(uint8_t)(g_keyboard_state.root_stack_count - 1U)] & 7U;
        if (((g_keyboard_state.note_mask >> slot) & 0x1U) != 0U)
        {
            if (out_slot != NULL)
            {
                *out_slot = slot;
            }
            return true;
        }
        g_keyboard_state.root_stack_count--;
    }

    return false;
}

static void kbd_flush_sounding_notes(void)
{
    for (uint8_t i = 0U; i < g_keyboard_state.sounding_count; ++i)
    {
        kbd_sink_note_off(g_keyboard_state.sounding[i]);
    }
    g_keyboard_state.sounding_count = 0U;
}

static void kbd_sink_all_notes_off_internal(void)
{
    kbd_flush_sounding_notes();
    if (g_keyboard_state.sink.all_notes_off != NULL)
    {
        g_keyboard_state.sink.all_notes_off();
    }
}

static inline uint8_t kbd_root_pc(void)
{
    return (uint8_t)(g_keyboard_state.ui_root_midi % 12U);
}

static bool kbd_pc_in_current_scale(uint8_t pc)
{
    if (g_keyboard_state.ui_scale == KBD_SCALE_CHROMATIC)
    {
        return true;
    }

    const uint8_t base_pc = kbd_root_pc();
    for (uint8_t slot = 0U; slot < 8U; ++slot)
    {
        const uint8_t scale_pc = (uint8_t)((base_pc + (uint8_t)kbd_scale_slot_semitone_offset((uint8_t)g_keyboard_state.ui_scale, slot)) % 12U);
        if (scale_pc == pc)
        {
            return true;
        }
    }

    return false;
}

static uint8_t kbd_quantize_to_current_scale(uint8_t midi_note)
{
    if (g_keyboard_state.ui_scale == KBD_SCALE_CHROMATIC)
    {
        return midi_note;
    }

    const uint8_t pc = (uint8_t)(midi_note % 12U);
    if (kbd_pc_in_current_scale(pc))
    {
        return midi_note;
    }

    int8_t up = 1;
    int8_t down = 1;

    while (up < 12)
    {
        const uint8_t test_pc = (uint8_t)((pc + (uint8_t)up) % 12U);
        if (kbd_pc_in_current_scale(test_pc))
        {
            break;
        }
        ++up;
    }

    while (down < 12)
    {
        const uint8_t test_pc = (uint8_t)((pc + 12U - (uint8_t)down) % 12U);
        if (kbd_pc_in_current_scale(test_pc))
        {
            break;
        }
        ++down;
    }

    int16_t corrected = (int16_t)midi_note + ((down <= up) ? (int8_t)(-down) : up);
    if (corrected < 0)
    {
        corrected = 0;
    }
    else if (corrected > 127)
    {
        corrected = 127;
    }

    return (uint8_t)corrected;
}

static int8_t kbd_slot_to_semitone_offset(uint8_t slot, bool high_row)
{
    slot &= 7U;
    if (g_keyboard_state.note_order == NOTE_ORDER_NATURAL)
    {
        int8_t offset = kbd_scale_slot_semitone_offset((uint8_t)g_keyboard_state.ui_scale, slot);
        if (high_row)
        {
            offset += 12;
        }
        return offset;
    }

    int16_t semitone;
    if (slot < 7U)
    {
        semitone = (int16_t)((7U * slot) % 12U);
    }
    else
    {
        semitone = 12;
    }

    if (high_row)
    {
        semitone += 12;
    }

    return (int8_t)semitone;
}

static inline int16_t kbd_apply_octave_shift(int16_t raw)
{
    int16_t shifted = raw + ((int16_t)g_keyboard_state.octave_shift * 12);
    if (shifted < 0)
    {
        shifted = 0;
    }
    else if (shifted > 127)
    {
        shifted = 127;
    }
    return shifted;
}

static void kbd_build_current_notes(uint8_t *out,
                                    uint8_t *out_count,
                                    ui_keyboard_active_chord_t *out_active)
{
    *out_count = 0U;
    out_active->valid = false;
    out_active->interval_count = 0U;
    out_active->root_midi = 0U;
    out_active->chord_mask = 0U;

    if (!g_keyboard_state.omnichord)
    {
        for (uint8_t slot = 0U; (slot < 16U) && (*out_count < UI_KEYBOARD_MAX_ACTIVE_NOTES); ++slot)
        {
            if (((g_keyboard_state.note_mask >> slot) & 0x1U) == 0U)
            {
                continue;
            }

            const bool high = (slot < 8U);
            const uint8_t local_slot = (uint8_t)(slot & 7U);
            int16_t raw = (int16_t)g_keyboard_state.ui_root_midi + kbd_slot_to_semitone_offset(local_slot, high);
            raw = kbd_apply_octave_shift(raw);

            uint8_t note = (uint8_t)raw;
            if (g_keyboard_state.note_order == NOTE_ORDER_FIFTHS)
            {
                note = kbd_quantize_to_current_scale(note);
            }

            kbd_add_note_unique(out, out_count, note);
        }
        return;
    }

    if ((g_keyboard_state.note_mask & 0x00FFU) == 0U)
    {
        return;
    }

    if (g_keyboard_state.chord_mask == 0U)
    {
        for (uint8_t slot = 0U; (slot < 8U) && (*out_count < UI_KEYBOARD_MAX_ACTIVE_NOTES); ++slot)
        {
            if (((g_keyboard_state.note_mask >> slot) & 0x1U) == 0U)
            {
                continue;
            }

            int16_t raw = (int16_t)g_keyboard_state.ui_root_midi + kbd_slot_to_semitone_offset(slot, false);
            raw = kbd_apply_octave_shift(raw);

            uint8_t note = (uint8_t)raw;
            if (g_keyboard_state.note_order == NOTE_ORDER_FIFTHS)
            {
                note = kbd_quantize_to_current_scale(note);
            }

            kbd_add_note_unique(out, out_count, note);
        }
        return;
    }

    uint8_t intervals[KBD_MAX_VOICING_NOTES];
    uint8_t interval_count = 0U;
    if (!kbd_chords_dict_build(g_keyboard_state.chord_mask, intervals, &interval_count) || (interval_count == 0U))
    {
        return;
    }

    uint8_t slot = 0U;
    if (!kbd_root_stack_active_slot(&slot))
    {
        return;
    }

    int16_t root = (int16_t)g_keyboard_state.ui_root_midi + kbd_slot_to_semitone_offset(slot, false);
    root = kbd_apply_octave_shift(root);

    out_active->root_midi = (uint8_t)root;
    out_active->chord_mask = g_keyboard_state.chord_mask;
    out_active->interval_count = interval_count;
    for (uint8_t i = 0U; i < interval_count; ++i)
    {
        out_active->intervals[i] = intervals[i];
    }
    out_active->valid = true;

    for (uint8_t i = 0U; (i < interval_count) && (*out_count < UI_KEYBOARD_MAX_ACTIVE_NOTES); ++i)
    {
        int16_t raw = root + intervals[i];
        if (raw < 0)
        {
            raw = 0;
        }
        else if (raw > 127)
        {
            raw = 127;
        }

        uint8_t note = (uint8_t)raw;
        if (!g_keyboard_state.chord_override)
        {
            note = kbd_quantize_to_current_scale(note);
        }

        kbd_add_note_unique(out, out_count, note);
    }
}

static void kbd_apply_sounding_delta(const uint8_t *notes, uint8_t note_count)
{
    uint8_t desired[128] = {0U};
    uint8_t sounding[128] = {0U};

    if (notes == NULL)
    {
        note_count = 0U;
    }

    for (uint8_t i = 0U; i < g_keyboard_state.sounding_count; ++i)
    {
        const uint8_t note = (uint8_t)(g_keyboard_state.sounding[i] & 0x7FU);
        if (sounding[note] < 0xFFU)
        {
            sounding[note]++;
        }
    }

    for (uint8_t i = 0U; i < note_count; ++i)
    {
        desired[(uint8_t)(notes[i] & 0x7FU)] = 1U;
    }

    for (uint8_t note = 0U; note < 128U; ++note)
    {
        while (sounding[note] > desired[note])
        {
            kbd_sink_note_off(note);
            sounding[note]--;
        }
        while (sounding[note] < desired[note])
        {
            kbd_sink_note_on(note);
            sounding[note]++;
        }
    }

    g_keyboard_state.sounding_count = 0U;
    for (uint8_t i = 0U; (i < note_count) && (g_keyboard_state.sounding_count < UI_KEYBOARD_MAX_ACTIVE_NOTES); ++i)
    {
        kbd_add_note_unique(g_keyboard_state.sounding, &g_keyboard_state.sounding_count, (uint8_t)(notes[i] & 0x7FU));
    }
}

static void kbd_refresh_sounding_state(void)
{
    if (g_keyboard_state.note_mask == 0U)
    {
        kbd_flush_sounding_notes();
        g_keyboard_state.active.valid = false;
        g_keyboard_state.active.interval_count = 0U;
        g_keyboard_state.active.root_midi = 0U;
        g_keyboard_state.active.chord_mask = 0U;
        return;
    }

    uint8_t notes[UI_KEYBOARD_MAX_ACTIVE_NOTES];
    uint8_t note_count = 0U;
    ui_keyboard_active_chord_t active;
    kbd_build_current_notes(notes, &note_count, &active);
    kbd_apply_sounding_delta(notes, note_count);
    g_keyboard_state.active = active;
}

void ui_keyboard_app_init(const ui_keyboard_note_sink_t *sink)
{
    memset(&g_keyboard_state, 0, sizeof(g_keyboard_state));
    g_keyboard_state.ui_root_midi = 60U;
    g_keyboard_state.ui_scale = KBD_SCALE_MAJOR;
    g_keyboard_state.note_order = NOTE_ORDER_NATURAL;
    g_keyboard_state.sink.velocity = 100U;

    if (sink != NULL)
    {
        g_keyboard_state.sink = *sink;
    }
}

void ui_keyboard_app_set_params(uint8_t root_midi, kbd_scale_t scale, bool omnichord)
{
    const bool omni_changed = (g_keyboard_state.omnichord != omnichord);
    g_keyboard_state.ui_root_midi = root_midi;
    g_keyboard_state.ui_scale = scale;
    g_keyboard_state.omnichord = omnichord;

    if (omni_changed)
    {
        kbd_flush_sounding_notes();
        g_keyboard_state.chord_mask = 0U;
        g_keyboard_state.note_mask = 0U;
        kbd_root_stack_clear();
        g_keyboard_state.active.valid = false;
        g_keyboard_state.active.interval_count = 0U;
        g_keyboard_state.active.root_midi = 0U;
        g_keyboard_state.active.chord_mask = 0U;
        return;
    }

    if (g_keyboard_state.note_mask != 0U)
    {
        kbd_refresh_sounding_state();
    }
}

void ui_keyboard_app_set_observer(ui_keyboard_chord_cb_t cb)
{
    g_keyboard_state.observer = cb;
}

void ui_keyboard_app_set_note_order(note_order_t order)
{
    if (g_keyboard_state.note_order == order)
    {
        return;
    }

    g_keyboard_state.note_order = order;
    if (g_keyboard_state.note_mask != 0U)
    {
        kbd_refresh_sounding_state();
    }
}

void ui_keyboard_app_set_chord_override(bool enable)
{
    if (g_keyboard_state.chord_override == enable)
    {
        return;
    }

    g_keyboard_state.chord_override = enable;
    if ((g_keyboard_state.note_mask != 0U) && (g_keyboard_state.chord_mask != 0U))
    {
        kbd_refresh_sounding_state();
    }
}

void ui_keyboard_app_set_velocity(uint8_t velocity)
{
    g_keyboard_state.sink.velocity = (velocity == 0U) ? 1U : velocity;
}

void ui_keyboard_app_set_octave_shift(int8_t shift)
{
    if (shift < CUSTOM_KEYS_OCT_SHIFT_MIN)
    {
        shift = CUSTOM_KEYS_OCT_SHIFT_MIN;
    }
    if (shift > CUSTOM_KEYS_OCT_SHIFT_MAX)
    {
        shift = CUSTOM_KEYS_OCT_SHIFT_MAX;
    }
    if (g_keyboard_state.octave_shift == shift)
    {
        return;
    }

    g_keyboard_state.octave_shift = shift;
    if (g_keyboard_state.note_mask != 0U)
    {
        kbd_refresh_sounding_state();
    }
}

int8_t ui_keyboard_app_get_octave_shift(void)
{
    return g_keyboard_state.octave_shift;
}

void ui_keyboard_app_note_button(uint8_t note_slot, bool pressed)
{
    if (!g_keyboard_state.omnichord)
    {
        const uint16_t bit = (uint16_t)(1U << (note_slot & 15U));
        if (pressed)
        {
            g_keyboard_state.note_mask |= bit;
        }
        else
        {
            g_keyboard_state.note_mask &= (uint16_t)(~bit);
        }

        kbd_refresh_sounding_state();
        return;
    }

    const uint16_t bit = (uint16_t)(1U << (note_slot & 7U));
    if (pressed)
    {
        g_keyboard_state.note_mask |= bit;
        kbd_root_stack_press(note_slot);
    }
    else
    {
        g_keyboard_state.note_mask &= (uint16_t)(~bit);
        kbd_root_stack_remove(note_slot);
    }

    ui_keyboard_active_chord_t previous = g_keyboard_state.active;
    kbd_refresh_sounding_state();

    if ((g_keyboard_state.observer != NULL)
            && ((previous.valid != g_keyboard_state.active.valid)
                || (previous.root_midi != g_keyboard_state.active.root_midi)
                || (previous.chord_mask != g_keyboard_state.active.chord_mask)
                || (previous.interval_count != g_keyboard_state.active.interval_count)
                || (memcmp(previous.intervals,
                           g_keyboard_state.active.intervals,
                           g_keyboard_state.active.interval_count) != 0)))
    {
        g_keyboard_state.observer(&g_keyboard_state.active);
    }
}

void ui_keyboard_app_chord_button(uint8_t chord_index, bool pressed)
{
    const uint8_t bit = (uint8_t)(1U << (chord_index & 7U));
    if (pressed)
    {
        g_keyboard_state.chord_mask |= bit;
    }
    else
    {
        g_keyboard_state.chord_mask &= (uint8_t)(~bit);
    }

    ui_keyboard_active_chord_t previous = g_keyboard_state.active;
    kbd_refresh_sounding_state();

    if ((g_keyboard_state.observer != NULL)
            && ((previous.valid != g_keyboard_state.active.valid)
                || (previous.root_midi != g_keyboard_state.active.root_midi)
                || (previous.chord_mask != g_keyboard_state.active.chord_mask)
                || (previous.interval_count != g_keyboard_state.active.interval_count)
                || (memcmp(previous.intervals,
                           g_keyboard_state.active.intervals,
                           g_keyboard_state.active.interval_count) != 0)))
    {
        g_keyboard_state.observer(&g_keyboard_state.active);
    }
}

void ui_keyboard_app_all_notes_off(void)
{
    kbd_sink_all_notes_off_internal();
    g_keyboard_state.active.valid = false;
    g_keyboard_state.active.interval_count = 0U;
    g_keyboard_state.active.root_midi = 0U;
    g_keyboard_state.active.chord_mask = 0U;
    g_keyboard_state.note_mask = 0U;
    g_keyboard_state.chord_mask = 0U;
    kbd_root_stack_clear();
}

void ui_keyboard_app_clear_state_silent(void)
{
    g_keyboard_state.sounding_count = 0U;
    g_keyboard_state.active.valid = false;
    g_keyboard_state.active.interval_count = 0U;
    g_keyboard_state.active.root_midi = 0U;
    g_keyboard_state.active.chord_mask = 0U;
    g_keyboard_state.note_mask = 0U;
    g_keyboard_state.chord_mask = 0U;
    kbd_root_stack_clear();
}

const ui_keyboard_active_chord_t *ui_keyboard_app_get_active_chord(void)
{
    return &g_keyboard_state.active;
}

void ui_keyboard_app_format_active_chord_label(char *out, uint32_t out_len)
{
    static const char *const k_root_names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (!g_keyboard_state.omnichord || !g_keyboard_state.active.valid)
    {
        (void)snprintf(out, out_len, "KBD");
        return;
    }

    const uint8_t root_pc = (uint8_t)(g_keyboard_state.active.root_midi % 12U);
    const char *const suffix = kbd_chords_dict_suffix(g_keyboard_state.active.chord_mask);
    if ((strcmp(suffix, "JAZZ") == 0) || (strcmp(suffix, "WTF") == 0))
    {
        (void)snprintf(out, out_len, "%s", suffix);
        return;
    }
    (void)snprintf(out, out_len, "%s%s", k_root_names[root_pc], suffix);
}

void ui_keyboard_app_tick(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
}
