/**
 * @file ui_keyboard_app.h
 * @brief App Keyboard (Custom Keys + Omnichord) — logique de notes/accords, options page 2, octave shift.
 * @ingroup ui_apps
 *
 * @details
 * - Omnichord OFF : pads → degrés de la gamme (2 lignes = deux octaves).
 * - Omnichord ON  : Chord buttons (triades + extensions) + zone Notes = root de l’accord.
 * - Quantisation commune OFF/ON (diatonique), tie-break vers le bas, clamp [0..127].
 * - Options page 2 : Note order (Natural/Fifths), Chord override (accidentals autorisés).
 * - Octave Shift : décalage global de toutes les notes, ±12 par cran, borné [-4..+4], base C4 (60) à 0.
 */

#ifndef BRICK_UI_KEYBOARD_APP_H
#define BRICK_UI_KEYBOARD_APP_H

#include <stdint.h>
#include <stdbool.h>

/** Identifiants de gammes (alignés avec kbd_chords_dict). */
typedef enum {
  KBD_SCALE_MAJOR = 0,
  KBD_SCALE_NAT_MINOR,
  KBD_SCALE_DORIAN,
  KBD_SCALE_MIXOLYDIAN,
  KBD_SCALE_PENT_MAJOR,
  KBD_SCALE_PENT_MINOR,
  KBD_SCALE_CHROMATIC
} kbd_scale_t;

/** @defgroup ui_keyboard_app UI Keyboard App
 *  @ingroup ui_apps
 *  @{
 */

/** Ordre des notes pour le layout des pads (page 2). */
typedef enum {
  NOTE_ORDER_NATURAL = 0,  /**< ordre naturel de la gamme courante */
  NOTE_ORDER_FIFTHS  = 1   /**< cycle des quintes depuis la root (root, +7, +14, ...) */
} note_order_t;

/** Bornes recommandées pour l’octave shift. */
#define CUSTOM_KEYS_OCT_SHIFT_MIN   (-4)
#define CUSTOM_KEYS_OCT_SHIFT_MAX   (+4)

/** Représentation de l'accord actif (observable UI). */
typedef struct {
  bool    valid;            /**< true si accord effectif (≥1 triade + ≥1 note) */
  uint8_t root_midi;        /**< Root absolue MIDI (0..127) */
  uint8_t intervals[12];    /**< Intervalles (demi-tons) depuis la root */
  uint8_t interval_count;   /**< Taille utile de intervals[] */
} ui_keyboard_active_chord_t;

/** Callback observateur pour notifier l’état de l’accord actif. */
typedef void (*ui_keyboard_chord_cb_t)(const ui_keyboard_active_chord_t *chord);

/** Sink neutre (fourni par ui_keyboard_bridge → ui_backend). */
typedef struct {
  void (*note_on)(uint8_t ch, uint8_t note, uint8_t vel);
  void (*note_off)(uint8_t ch, uint8_t note, uint8_t vel);
  void (*all_notes_off)(uint8_t ch);
  uint8_t midi_channel;
  uint8_t velocity;
} ui_keyboard_note_sink_t;

/** Initialisation de l’app Keyboard (registre le sink MIDI). */
void ui_keyboard_app_init(const ui_keyboard_note_sink_t *sink);

/**
 * @brief Met à jour Root/Scale/Omnichord.
 * @param root_midi Root absolue MIDI (ex. C4 = 60)
 * @param scale     Identifiant de gamme
 * @param omnichord true pour activer le mode Omnichord
 */
void ui_keyboard_app_set_params(uint8_t root_midi, kbd_scale_t scale, bool omnichord);

/** Enregistre le callback d’observation d’accord (optionnel). */
void ui_keyboard_app_set_observer(ui_keyboard_chord_cb_t cb);

/** Réglage page 2 : ordre des notes (Natural vs Circle of Fifths). */
void ui_keyboard_app_set_note_order(note_order_t order);

/**
 * @brief Réglage page 2 : comportement Orchid — les Chord Buttons peuvent déroger à la gamme.
 * @param enable true = allow accidentals (bypass quantization pour les notes d'accords Omnichord)
 */
void ui_keyboard_app_set_chord_override(bool enable);

/* === Octave shift (Custom Keys) ========================================= */

/**
 * @brief Définit le décalage d’octave global (appliqué à toutes les notes).
 * @param shift Octaves dans [-4..+4] ; 0 = base C4
 */
void ui_keyboard_app_set_octave_shift(int8_t shift);

/** @brief Récupère l’octave shift courant (pour l’overlay). */
int8_t ui_keyboard_app_get_octave_shift(void);

/* === Entrées (pads / chord buttons) ===================================== */

/**
 * @brief Note button press/release.
 * @param note_slot
 *  - **Omni OFF** : 0..15 (0..7 = ligne **haute** / +12 ; 8..15 = ligne **basse** / 0)
 *  - **Omni ON**  : 0..7  (7 degrés + octave root)
 */
void ui_keyboard_app_note_button(uint8_t note_slot, bool pressed);

/**
 * @brief Chord button press/release (Omni ON uniquement).
 * @param chord_index 0..7 (0..3 = bases Maj/Min/Sus4/Dim, 4..7 = extensions 7/M7/6/9).
 */
void ui_keyboard_app_chord_button(uint8_t chord_index, bool pressed);

/** Coupe toutes les notes actives. */
void ui_keyboard_app_all_notes_off(void);

/** Retourne un pointeur constant sur l’accord actif. */
const ui_keyboard_active_chord_t *ui_keyboard_app_get_active_chord(void);

/** Tick optionnel (placeholder pour évolutions). */
void ui_keyboard_app_tick(uint32_t elapsed_ms);

/** @} */

#endif /* BRICK_UI_KEYBOARD_APP_H */
