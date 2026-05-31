#ifndef BRICK_UI_KEYBOARD_APP_H
#define BRICK_UI_KEYBOARD_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KBD_SCALE_MAJOR = 0,
    KBD_SCALE_NAT_MINOR,
    KBD_SCALE_DORIAN,
    KBD_SCALE_MIXOLYDIAN,
    KBD_SCALE_PENT_MAJOR,
    KBD_SCALE_PENT_MINOR,
    KBD_SCALE_CHROMATIC
} kbd_scale_t;

typedef enum {
    NOTE_ORDER_NATURAL = 0,
    NOTE_ORDER_FIFTHS = 1
} note_order_t;

#define CUSTOM_KEYS_OCT_SHIFT_MIN   (-4)
#define CUSTOM_KEYS_OCT_SHIFT_MAX   (+4)
#define UI_KEYBOARD_MAX_ACTIVE_NOTES 16U

typedef struct {
    bool valid;
    uint8_t root_midi;
    uint8_t chord_mask;
    uint8_t intervals[12];
    uint8_t interval_count;
} ui_keyboard_active_chord_t;

typedef void (*ui_keyboard_chord_cb_t)(const ui_keyboard_active_chord_t *chord);

typedef struct {
    void (*note_on)(uint8_t note, uint8_t velocity);
    void (*note_off)(uint8_t note);
    void (*all_notes_off)(void);
    uint8_t velocity;
} ui_keyboard_note_sink_t;

void ui_keyboard_app_init(const ui_keyboard_note_sink_t *sink);
void ui_keyboard_app_set_params(uint8_t root_midi, kbd_scale_t scale, bool omnichord);
void ui_keyboard_app_set_observer(ui_keyboard_chord_cb_t cb);
void ui_keyboard_app_set_note_order(note_order_t order);
void ui_keyboard_app_set_chord_override(bool enable);
void ui_keyboard_app_set_velocity(uint8_t velocity);
void ui_keyboard_app_set_octave_shift(int8_t shift);
int8_t ui_keyboard_app_get_octave_shift(void);
void ui_keyboard_app_note_button(uint8_t note_slot, bool pressed);
void ui_keyboard_app_chord_button(uint8_t chord_index, bool pressed);
void ui_keyboard_app_all_notes_off(void);
void ui_keyboard_app_clear_state_silent(void);
const ui_keyboard_active_chord_t *ui_keyboard_app_get_active_chord(void);
void ui_keyboard_app_format_active_chord_label(char *out, uint32_t out_len);
void ui_keyboard_app_tick(uint32_t elapsed_ms);

#endif /* BRICK_UI_KEYBOARD_APP_H */
