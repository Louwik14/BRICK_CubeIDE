/**
 * @file kbd_input_mapper.h
 * @brief Mapper SEQ1..16 → app Keyboard (Omnichord ON/OFF).
 * @ingroup ui_apps
 *
 * @details
 * Omnichord OFF :
 *  - Ligne haute (SEQ1..8)  : 7 degrés + octave root (SEQ8) → **octave HAUTE**
 *  - Ligne basse (SEQ9..16) : 7 degrés + octave root (SEQ16) → **octave BASSE**
 *  → L’app reçoit des note_slots 0..15 (0..7 = haut / +12, 8..15 = bas / 0).
 *
 * Omnichord ON :
 *  - Chord Zone basse : SEQ1..4  → Bases (Maj/Min/Sus4/Dim)
 *  - Chord Zone haute : SEQ9..12 → Extensions (7/Maj7/6/9)
 *  - Note Zone        : SEQ5..8 + SEQ13..16 → 7 degrés + octave root (SEQ16)
 */

#ifndef BRICK_UI_KBD_INPUT_MAPPER_H
#define BRICK_UI_KBD_INPUT_MAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void kbd_input_mapper_init(bool omnichord_state);
void kbd_input_mapper_set_omnichord_state(bool enabled);
void kbd_input_mapper_process(uint8_t seq_index, bool pressed);

#ifdef __cplusplus
}
#endif

#endif /* BRICK_UI_KBD_INPUT_MAPPER_H */
