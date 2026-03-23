/**
 * @file kbd_input_mapper.c
 * @brief Implémentation du mapper SEQ1..16 → app Keyboard (Omnichord ON/OFF).
 * @ingroup ui_apps
 */

#include "kbd_input_mapper.h"
#include "ui_keyboard_app.h"

static bool s_omnichord = false;

void kbd_input_mapper_init(bool omnichord_state){ s_omnichord = omnichord_state; }
void kbd_input_mapper_set_omnichord_state(bool enabled){ s_omnichord = enabled; }

void kbd_input_mapper_process(uint8_t seq_index, bool pressed){
  if (seq_index < 1 || seq_index > 16) return;
  const uint8_t idx = (uint8_t)(seq_index - 1); /* 0..15 */

  if (!s_omnichord){
    /* OMNI OFF : 0..7 = ligne haute (+12), 8..15 = ligne basse (0) */
    ui_keyboard_app_note_button(idx, pressed);
    return;
  }

  /* OMNI ON */
  if (idx <= 3){ ui_keyboard_app_chord_button(idx, pressed); return; }             /* SEQ1..4 → bases */
  if (idx >= 8 && idx <= 11){ ui_keyboard_app_chord_button((uint8_t)(idx - 4), pressed); return; } /* SEQ9..12 → 4..7 */

  /* Note zone : SEQ5..8 (idx 4..7 → slots 0..3), SEQ13..16 (idx 12..15 → slots 4..7) */
  if (idx >= 4 && idx <= 7){ ui_keyboard_app_note_button((uint8_t)(idx - 4), pressed); return; }   /* 0..3 */
  if (idx >= 12 && idx <= 14){ ui_keyboard_app_note_button((uint8_t)(idx - 8), pressed); return; } /* 4..6 */
  if (idx == 15){ ui_keyboard_app_note_button(7u, pressed); return; }                                /* octave root */
}
