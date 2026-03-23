/**
 * @file kbd_chords_dict.h
 * @brief Dictionnaire d'accords et utilitaires de transposition pour l'app Keyboard (inspiré Orchid).
 * @ingroup ui_apps
 */

#ifndef BRICK_UI_KBD_CHORDS_DICT_H
#define BRICK_UI_KBD_CHORDS_DICT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gammes (aligné ui_keyboard_app) */
enum {
  KBD_SCALE_ID_MAJOR = 0,
  KBD_SCALE_ID_NAT_MINOR,
  KBD_SCALE_ID_DORIAN,
  KBD_SCALE_ID_MIXOLYDIAN,
  KBD_SCALE_ID_PENT_MAJOR,
  KBD_SCALE_ID_PENT_MINOR,
  KBD_SCALE_ID_CHROMATIC
};

enum {
  KBD_SCALE_COUNT = KBD_SCALE_ID_CHROMATIC + 1
};

#define KBD_SCALE_SLOT_COUNT 8u

/* Masque chord-buttons (8 bits) */
#define KBD_CH_BASE_MAJOR   (1u << 0) /* [0,4,7]  */
#define KBD_CH_BASE_MINOR   (1u << 1) /* [0,3,7]  */
#define KBD_CH_BASE_SUS4    (1u << 2) /* [0,5,7]  */
#define KBD_CH_BASE_DIM     (1u << 3) /* [0,3,6]  */

#define KBD_CH_EXT_7        (1u << 4) /* [+10]    */
#define KBD_CH_EXT_MAJ7     (1u << 5) /* [+11]    */
#define KBD_CH_EXT_6        (1u << 6) /* [+9]     */
#define KBD_CH_EXT_9        (1u << 7) /* [+14]    */

#define KBD_CH_MASK_BASES   (KBD_CH_BASE_MAJOR | KBD_CH_BASE_MINOR | KBD_CH_BASE_SUS4 | KBD_CH_BASE_DIM)
#define KBD_CH_MASK_EXTS    (KBD_CH_EXT_7 | KBD_CH_EXT_MAJ7 | KBD_CH_EXT_6 | KBD_CH_EXT_9)

bool  kbd_chords_dict_build(uint8_t chord_mask, uint8_t *intervals, uint8_t *count);
int8_t kbd_scale_slot_semitone_offset(uint8_t scale_id, uint8_t slot);

#ifdef TEST
bool kbd_chords_dict_selftest(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BRICK_UI_KBD_CHORDS_DICT_H */
