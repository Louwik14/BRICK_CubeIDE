/**
 * @file ui_keyboard_app.c
 * @brief App Keyboard — moteur Omnichord-style (Orchid) + clavier scalaire, octave shift & label.
 * @ingroup ui_apps
 *
 * @details
 * - Omnichord OFF : pads → degrés de la gamme (2 lignes = deux octaves).
 * - Omnichord ON  : pads de notes + boutons d’accords; extensions seules → pas de son.
 * - Quantisation commune OFF/ON; tie-break vers le bas; clamp [0..127].
 * - Options page 2 : Note order (Natural/Fifths), Chord override (accidentals).
 * - Octave Shift : appliqué avant quantisation/clamp, borné [-4..+4].
 */

#include "ui_keyboard_app.h"

#include <string.h>
#include "brick_config.h"
#include "ui_led_backend.h"
#include "kbd_chords_dict.h"

#define KBD_MAX_VOICING_NOTES 12
#define KBD_MAX_ACTIVE_NOTES  16

/* ============================ State interne ============================== */

typedef struct {
  bool        omnichord;
  uint8_t     ui_root_midi;     /* Root absolue (0..127) — base C4 + offset UI */
  kbd_scale_t ui_scale;         /* Identifiant de gamme */

  note_order_t note_order;      /* Natural vs Fifths (page 2) */
  bool         chord_override;  /* true = bypass quantization pour accords Omnichord */

  int8_t       octave_shift;    /* Décalage global en octaves [-4..+4] */

  uint8_t  chord_mask;          /* bits 0..7 : bases + extensions */
  uint16_t note_mask;           /* Omni OFF : 16 bits (0..15), Omni ON : 8 bits utiles */

  ui_keyboard_active_chord_t active;
  uint8_t sounding[KBD_MAX_ACTIVE_NOTES];
  uint8_t sounding_count;

  ui_keyboard_note_sink_t   sink;
  ui_keyboard_chord_cb_t    observer;
} kbd_state_t;

static CCM_DATA kbd_state_t g;

/* ============================== Note sink ================================ */

static inline void sink_note_on(uint8_t n){ if (g.sink.note_on)  g.sink.note_on(g.sink.midi_channel, n, g.sink.velocity); }
static inline void sink_note_off(uint8_t n){if (g.sink.note_off) g.sink.note_off(g.sink.midi_channel, n, 0); }

static void flush_sounding_notes(void){
  for (uint8_t i=0;i<g.sounding_count;++i) sink_note_off(g.sounding[i]);
  g.sounding_count = 0;
}

static void sink_all_notes_off_internal(void){
  flush_sounding_notes();
  if (g.sink.all_notes_off) g.sink.all_notes_off(g.sink.midi_channel);
}

/* ======================= Helpers gamme / quantization ==================== */

static inline uint8_t _root_pc(void){ return (uint8_t)(g.ui_root_midi % 12u); }

static bool _pc_in_current_scale(uint8_t pc){
  if (g.ui_scale == KBD_SCALE_CHROMATIC) return true;
  const uint8_t base_pc = _root_pc();
  for (uint8_t s = 0; s < 8; ++s){
    const uint8_t sc_pc = (uint8_t)((base_pc + (uint8_t)kbd_scale_slot_semitone_offset((uint8_t)g.ui_scale, s)) % 12u);
    if (sc_pc == pc) return true;
  }
  return false;
}

static uint8_t quantize_to_current_scale(uint8_t midi_note){
  if (g.ui_scale == KBD_SCALE_CHROMATIC) return midi_note; /* tout passe */

  const uint8_t pc = (uint8_t)(midi_note % 12u);
  if (_pc_in_current_scale(pc)) return midi_note;

  int8_t up = 1, down = 1;
  while (up < 12){
    const uint8_t test_pc = (uint8_t)((pc + up) % 12u);
    if (_pc_in_current_scale(test_pc)) break;
    ++up;
  }
  while (down < 12){
    const uint8_t test_pc = (uint8_t)((pc + 12u - down) % 12u);
    if (_pc_in_current_scale(test_pc)) break;
    ++down;
  }

  int16_t corrected = (int16_t)midi_note + ((down <= up) ? (int8_t)(-down) : up);
  if (corrected < 0) corrected = 0;
  else if (corrected > 127) corrected = 127;
  return (uint8_t)corrected;
}

/* ====================== Helpers mapping Natural / Fifths ================= */

static int8_t slot_to_semitone_offset(uint8_t slot, bool high_row){
  slot &= 7u;
  if (g.note_order == NOTE_ORDER_NATURAL){
    int8_t off = kbd_scale_slot_semitone_offset((uint8_t)g.ui_scale, slot);
    if (high_row) off += 12;
    return off;
  }

  /* Fifths : root, +7, +14, ..., +49, et le dernier slot (7) = +12 (boucle octave) */
  int16_t semitone;
  if (slot < 7) semitone = (int16_t)((7 * slot) % 12);
  else          semitone = 12;
  if (high_row) semitone += 12;
  return (int8_t)semitone;
}

/* ===================== Construction des notes actives ==================== */

static inline int16_t _apply_octave_shift(int16_t raw){
  int16_t out = raw + (int16_t)g.octave_shift * 12;
  if (out < 0) out = 0;
  else if (out > 127) out = 127;
  return out;
}

static void build_current_notes(uint8_t *out, uint8_t *out_count, ui_keyboard_active_chord_t *out_active){
  *out_count = 0;
  out_active->valid = false;
  out_active->interval_count = 0;
  out_active->root_midi = 0;

  if (!g.omnichord){
    /* Omni OFF : 2 lignes → deux octaves (ligne haute = +12, ligne basse = 0) */
    for (uint8_t s=0; s<16 && *out_count<KBD_MAX_ACTIVE_NOTES; ++s){
      if ((g.note_mask >> s) & 0x1){
        const bool high = (s < 8);
        const uint8_t slot = (uint8_t)(s & 7u);
        int16_t raw = (int16_t)g.ui_root_midi + slot_to_semitone_offset(slot, high);
        raw = _apply_octave_shift(raw);

        uint8_t note = (uint8_t)raw;
        if (g.note_order == NOTE_ORDER_FIFTHS) note = quantize_to_current_scale(note);
        out[(*out_count)++] = note;
      }
    }
    return;
  }

  /* Omni ON : si aucune note → rien */
  if ((g.note_mask & 0x00FFu) == 0) return;

  /* Sans chord → notes seules (ordre Natural/Fifths appliqué) */
  if (g.chord_mask == 0){
    for (uint8_t s=0; s<8 && *out_count<KBD_MAX_ACTIVE_NOTES; ++s){
      if ((g.note_mask >> s) & 0x1){
        int16_t raw = (int16_t)g.ui_root_midi + slot_to_semitone_offset(s, /*high_row=*/false);
        raw = _apply_octave_shift(raw);

        uint8_t note = (uint8_t)raw;
        if (g.note_order == NOTE_ORDER_FIFTHS) note = quantize_to_current_scale(note);
        out[(*out_count)++] = note;
      }
    }
    return;
  }

  /* Accord : combiner triades + extensions (Orchid) */
  uint8_t intervals[KBD_MAX_VOICING_NOTES], n_int=0;
  if (!kbd_chords_dict_build(g.chord_mask, intervals, &n_int) || n_int==0) return; /* extensions seules → invalide */

  for (uint8_t s=0; s<8 && *out_count<KBD_MAX_ACTIVE_NOTES; ++s){
    if ((g.note_mask >> s) & 0x1){
      int16_t root = (int16_t)g.ui_root_midi + slot_to_semitone_offset(s, /*high_row=*/false);
      root = _apply_octave_shift(root);

      out_active->root_midi = (uint8_t)root;
      out_active->interval_count = n_int;
      for (uint8_t i=0;i<n_int;++i) out_active->intervals[i] = intervals[i];
      out_active->valid = true;

      for (uint8_t i=0;i<n_int && *out_count<KBD_MAX_ACTIVE_NOTES;++i){
        int16_t raw = root + intervals[i];
        if (raw < 0) raw = 0; else if (raw > 127) raw = 127;

        uint8_t note = (uint8_t)raw;
        if (!g.chord_override) note = quantize_to_current_scale(note);
        if (note > 127) note = 127;
        out[(*out_count)++] = note;
      }
    }
  }
}

/* ============================== Delta ON/OFF ============================== */

static void apply_sounding_delta(const uint8_t *nn, uint8_t ncount){
  /* OFF ce qui n'est plus là */
  for (uint8_t i=0;i<g.sounding_count;++i){
    const uint8_t oldn = g.sounding[i];
    bool still=false; for(uint8_t j=0;j<ncount;++j) if (nn[j]==oldn){still=true;break;}
    if (!still) sink_note_off(oldn);
  }
  /* ON ce qui est nouveau */
  for (uint8_t j=0;j<ncount;++j){
    const uint8_t n = nn[j];
    bool was_on=false; for(uint8_t i=0;i<g.sounding_count;++i) if (g.sounding[i]==n){was_on=true;break;}
    if (!was_on) sink_note_on(n);
  }
  /* maj cache */
  g.sounding_count = (ncount > KBD_MAX_ACTIVE_NOTES) ? KBD_MAX_ACTIVE_NOTES : ncount;
  for (uint8_t i=0;i<g.sounding_count;++i) g.sounding[i] = nn[i];
}

/* ================================ API ==================================== */

void ui_keyboard_app_init(const ui_keyboard_note_sink_t *sink){
  memset(&g,0,sizeof(g));
  g.ui_root_midi   = 60; /* C4 */
  g.ui_scale       = KBD_SCALE_MAJOR;
  g.omnichord      = false;
  g.note_order     = NOTE_ORDER_NATURAL;
  g.chord_override = false;
  g.octave_shift   = 0;

  if (sink) g.sink = *sink; else {
    g.sink.note_on = NULL; g.sink.note_off = NULL; g.sink.all_notes_off = NULL;
    g.sink.midi_channel = 0; g.sink.velocity = 100;
  }
  ui_led_backend_set_keyboard_omnichord(g.omnichord);
}

void ui_keyboard_app_set_params(uint8_t root_midi, kbd_scale_t scale, bool omnichord){
  const bool omni_changed = (g.omnichord != omnichord);
  g.ui_root_midi = root_midi;
  g.ui_scale     = scale;
  g.omnichord    = omnichord;
  if (omni_changed){
    flush_sounding_notes();
    g.chord_mask = 0;
    g.note_mask  = 0;
    ui_led_backend_set_keyboard_omnichord(g.omnichord);
  }
}

void ui_keyboard_app_set_observer(ui_keyboard_chord_cb_t cb){ g.observer = cb; }

void ui_keyboard_app_set_note_order(note_order_t order){
  if (g.note_order == order) return;
  g.note_order = order;
  if (g.note_mask){
    uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
    build_current_notes(notes,&cnt,&na);
    apply_sounding_delta(notes,cnt);
    g.active = na;
  }
}

void ui_keyboard_app_set_chord_override(bool enable){
  if (g.chord_override == enable) return;
  g.chord_override = enable;
  if (g.note_mask && g.chord_mask){
    uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
    build_current_notes(notes,&cnt,&na);
    apply_sounding_delta(notes,cnt);
    g.active = na;
  }
}

void ui_keyboard_app_all_notes_off(void){
  sink_all_notes_off_internal();
  g.active.valid=false; g.active.interval_count=0; g.active.root_midi=0;
}

const ui_keyboard_active_chord_t *ui_keyboard_app_get_active_chord(void){ return &g.active; }
void ui_keyboard_app_tick(uint32_t elapsed_ms){ (void)elapsed_ms; }

/* -------- Octave shift public ------------------------------------------- */

void ui_keyboard_app_set_octave_shift(int8_t shift){
  if (shift < CUSTOM_KEYS_OCT_SHIFT_MIN) shift = CUSTOM_KEYS_OCT_SHIFT_MIN;
  if (shift > CUSTOM_KEYS_OCT_SHIFT_MAX) shift = CUSTOM_KEYS_OCT_SHIFT_MAX;
  if (g.octave_shift == shift) return;
  g.octave_shift = shift;

  if (g.note_mask){
    uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
    build_current_notes(notes,&cnt,&na);
    apply_sounding_delta(notes,cnt);
    g.active = na;
  }
}

int8_t ui_keyboard_app_get_octave_shift(void){
  return g.octave_shift;
}

/* -------- Entrées (pads / chord buttons) -------------------------------- */

void ui_keyboard_app_note_button(uint8_t note_slot, bool pressed){
  if (!g.omnichord){
    /* 0..15 (0..7 top/high +12 ; 8..15 bottom 0) */
    const uint16_t bit = (uint16_t)(1u << (note_slot & 15u));
    if (pressed) g.note_mask |= bit; else g.note_mask &= (uint16_t)(~bit);

    if (g.note_mask == 0){ flush_sounding_notes(); g.active.valid=false; g.active.interval_count=0; g.active.root_midi=0; return; }

    uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
    build_current_notes(notes,&cnt,&na);
    apply_sounding_delta(notes,cnt);
    g.active = na; /* simple notes → valid=false */
    return;
  }

  /* Omni ON : 0..7 (7 degrés + octave root) */
  const uint16_t bit = (uint16_t)(1u << (note_slot & 7u));
  if (pressed) g.note_mask |= bit; else g.note_mask &= (uint16_t)(~bit);

  if ((g.note_mask & 0x00FFu) == 0){ flush_sounding_notes(); g.active.valid=false; g.active.interval_count=0; g.active.root_midi=0; return; }

  uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
  build_current_notes(notes,&cnt,&na);
  apply_sounding_delta(notes,cnt);
  if (g.observer){
    if (na.valid != g.active.valid ||
        na.root_midi != g.active.root_midi ||
        na.interval_count != g.active.interval_count ||
        memcmp(na.intervals,g.active.intervals,na.interval_count)!=0){
      g.active = na; g.observer(&g.active);
    } else { g.active = na; }
  } else { g.active = na; }
}

void ui_keyboard_app_chord_button(uint8_t chord_index, bool pressed){
  const uint8_t bit = (uint8_t)(1u << (chord_index & 7u));
  if (pressed) g.chord_mask |= bit; else g.chord_mask &= (uint8_t)(~bit);

  if (g.note_mask){
    uint8_t notes[KBD_MAX_ACTIVE_NOTES], cnt; ui_keyboard_active_chord_t na;
    build_current_notes(notes,&cnt,&na);
    apply_sounding_delta(notes,cnt);
    if (g.observer){
      if (na.valid != g.active.valid ||
          na.root_midi != g.active.root_midi ||
          na.interval_count != g.active.interval_count ||
          memcmp(na.intervals,g.active.intervals,na.interval_count)!=0){
        g.active = na; g.observer(&g.active);
      } else { g.active = na; }
    } else { g.active = na; }
  } else {
    g.active.valid=false; g.active.interval_count=0; g.active.root_midi=0;
  }
}
