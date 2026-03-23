/**
 * @file ui_keyboard_bridge.c
 * @brief Bridge UI ↔ Keyboard App ↔ Backend, latence minimale (émission directe).
 * @ingroup ui_apps
 *
 * @details
 * - Lit Root / Scale / Omnichord via ui_backend_shadow_get() (dest UI).
 * - Lit aussi la page 2 : Note Order & Chord Override (IDs exposés par ui_keyboard_ui.h).
 * - Met à jour immédiatement l’app + le mapper + les LEDs à chaque itération.
 * - Le sink émet directement via ui_backend_note_on/off/all_notes_off → chemin court MIDI.
 */

#include "ui_keyboard_bridge.h"

#include "ui_keyboard_app.h"
#include "kbd_input_mapper.h"
#include "arp_engine.h" // --- ARP: moteur temps réel ---
#include "ui_arp_menu.h" // --- ARP: paramètres UI ---

/* Backend (fonctions directes) + constantes UI_DEST_* */
#include "ui_backend.h"

/* LEDs (observateur passif) */
#include "ui_led_backend.h"
#include "seq_recorder.h"

/* IDs UI de la vitrine Keyboard (omni/scale/root + page 2) */
#include "ui_keyboard_ui.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Si ui_backend.h n’expose pas (encore) ces 3 APIs “directes”, déclare-les  */
/* ici proprement pour éviter les 'implicit declaration' et garder le build. */
/* Remarque : tu peux aussi les ajouter dans ui_backend.h si tu préfères.    */
/* -------------------------------------------------------------------------- */
#ifndef UI_BACKEND_NOTE_API_DECLARED
#define UI_BACKEND_NOTE_API_DECLARED 1
extern void ui_backend_note_on(uint8_t note, uint8_t velocity);
extern void ui_backend_note_off(uint8_t note);
extern void ui_backend_all_notes_off(void);
#endif

/* ============================ Note sink (direct backend) ================== */

#define DEFAULT_MIDI_CHANNEL 0
#define DEFAULT_VELOCITY     100

static arp_engine_t s_arp_engine;                 // --- ARP: instance moteur ---
static arp_config_t s_arp_config;                 // --- ARP: configuration courante ---
static systime_t    s_last_group_stamp;           // --- ARP FIX: timestamp commun accords ---
static systime_t    s_last_group_seen;            // --- ARP FIX: détection burst ---

static void _arp_callback_note_on(uint8_t note, uint8_t vel, systime_t when);
static void _arp_callback_note_off(uint8_t note);

static const arp_callbacks_t k_arp_callbacks = {
  .note_on = _arp_callback_note_on,
  .note_off = _arp_callback_note_off
};

static inline uint8_t _resolve_velocity(uint8_t vel) {
  return (vel != 0u) ? vel : DEFAULT_VELOCITY;
}

static void _direct_note_on_at(uint8_t note, uint8_t vel, systime_t when) { // --- ARP FIX: timestamp groupé ---
  const uint8_t resolved = _resolve_velocity(vel);
  seq_recorder_handle_note_on_at(note, resolved, when);
  ui_backend_note_on(note, resolved);
}

static systime_t _capture_group_timestamp(void) {
  const systime_t now = chVTGetSystemTimeX();
  if (chTimeDiffX(s_last_group_seen, now) <= TIME_MS2I(1)) {
    s_last_group_seen = now;
    return s_last_group_stamp;
  }
  s_last_group_stamp = now;
  s_last_group_seen = now;
  return s_last_group_stamp;
}

static void _direct_note_on(uint8_t note, uint8_t vel) {
  const systime_t stamp = _capture_group_timestamp();
  _direct_note_on_at(note, vel, stamp);
}

static void _direct_note_off(uint8_t note) {
  const systime_t now = chVTGetSystemTimeX();
  seq_recorder_handle_note_off_at(note, now); // --- ARP FIX: wrapper timestamp ---
  ui_backend_note_off(note);
}

static void _arp_callback_note_on(uint8_t note, uint8_t vel, systime_t when) { // --- ARP: callback MIDI ---
  _direct_note_on_at(note, vel, when);
}

static void _arp_callback_note_off(uint8_t note) { // --- ARP: callback OFF ---
  _direct_note_off(note);
}

static void sink_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
  (void)ch;
  if (s_arp_config.enabled) {
    arp_note_input(&s_arp_engine, note, _resolve_velocity(vel), true);
  } else {
    _direct_note_on(note, vel);
  }
}

static void sink_note_off(uint8_t ch, uint8_t note, uint8_t vel) {
  (void)ch; (void)vel;
  if (s_arp_config.enabled) {
    arp_note_input(&s_arp_engine, note, 0u, false);
  } else {
    _direct_note_off(note);
  }
}

static void sink_all_notes_off(uint8_t ch) {
  (void)ch;
  arp_stop_all(&s_arp_engine); // --- ARP: flush immédiat ---
}

static ui_keyboard_note_sink_t g_sink = {
  .note_on = sink_note_on,
  .note_off = sink_note_off,
  .all_notes_off = sink_all_notes_off,
  .midi_channel = DEFAULT_MIDI_CHANNEL,
  .velocity = DEFAULT_VELOCITY
};

/* ============================ Helpers mapping UI ========================= */

/* IDs locaux tels qu’exposés par la vitrine Keyboard (voir ui_keyboard_ui.c) */
enum {
  KBD_UI_LOCAL_SCALE = 0x0100u, // --- FIX: aligner sur l’espace Keyboard isolé ---
  KBD_UI_LOCAL_ROOT  = 0x0101u,
  KBD_UI_LOCAL_ARP   = 0x0102u
};
/* Omnichord / Note Order / Chord Override sont exposés par ui_keyboard_ui.h :
   - KBD_OMNICHORD_ID
   - KBD_NOTE_ORDER_ID
   - KBD_CHORD_OVERRIDE_ID
   Ce sont des IDs **locaux** (sans UI_DEST_UI). */

#define KBD_UI_ID(local)  (uint16_t)(UI_DEST_UI | ((local) & 0x1FFFu))

/* Mapping vitrine → moteur (ordre comme dans ui_keyboard_ui.c) */
static kbd_scale_t _map_scale_from_ui(uint8_t ui_scale_id) {
  switch (ui_scale_id) {
    case 0: return KBD_SCALE_MAJOR;
    case 1: return KBD_SCALE_NAT_MINOR;
    case 2: return KBD_SCALE_PENT_MAJOR;
    case 3: return KBD_SCALE_DORIAN;
    case 4: return KBD_SCALE_MIXOLYDIAN;
    default: return KBD_SCALE_MAJOR;
  }
}

static inline uint8_t _shadow_ui_local(uint16_t local_id) {
  return ui_backend_shadow_get(KBD_UI_ID(local_id));
}

static inline uint8_t _shadow_arp_u8(uint16_t local_id) {
  return ui_backend_shadow_get(KBD_ARP_UI_ID(local_id));
}

static int8_t _shadow_arp_decode_range(uint16_t local_id, int min, int max) {
  const uint8_t raw = ui_backend_shadow_get(KBD_ARP_UI_ID(local_id));
  const int span = max - min;
  if (span <= 0) {
    return (int8_t)min;
  }
  int value = (int)raw;
  if (min >= 0 && max <= 255) {
    if (value < min) value = min;
    if (value > max) value = max;
    return (int8_t)value;
  }
  if (span == 255) {
    value = min + value;
    if (value < min) value = min;
    if (value > max) value = max;
    return (int8_t)value;
  }
  value = min + ((value * span + 127) / 255);
  if (value < min) value = min;
  if (value > max) value = max;
  return (int8_t)value; // --- ARP FIX: décodage continu depuis le shadow ---
}

static void _sync_arp_config_from_ui(void) { // --- ARP: lecture paramètres UI ---
  arp_config_t cfg = s_arp_config;
  cfg.enabled = (_shadow_ui_local(KBD_UI_LOCAL_ARP) != 0u); // --- ARP FIX: toggle global repris de la vitrine Keyboard ---
  cfg.hold_enabled = (_shadow_arp_u8(KBD_ARP_LOCAL_HOLD) != 0u);
  cfg.rate = (arp_rate_t)(_shadow_arp_u8(KBD_ARP_LOCAL_RATE) % ARP_RATE_COUNT);
  cfg.octave_range = _shadow_arp_u8(KBD_ARP_LOCAL_OCT_RANGE);
  cfg.pattern = (arp_pattern_t)(_shadow_arp_u8(KBD_ARP_LOCAL_PATTERN) % ARP_PATTERN_COUNT);
  cfg.gate_percent = _shadow_arp_u8(KBD_ARP_LOCAL_GATE);
  cfg.swing_percent = _shadow_arp_u8(KBD_ARP_LOCAL_SWING);
  cfg.accent = (arp_accent_t)(_shadow_arp_u8(KBD_ARP_LOCAL_ACCENT) % ARP_ACCENT_COUNT);
  cfg.vel_accent = _shadow_arp_u8(KBD_ARP_LOCAL_VEL_ACC);
  cfg.strum_mode = (arp_strum_t)(_shadow_arp_u8(KBD_ARP_LOCAL_STRUM_MODE) % ARP_STRUM_COUNT);
  cfg.strum_offset_ms = _shadow_arp_u8(KBD_ARP_LOCAL_STRUM_OFFSET);
  cfg.transpose = _shadow_arp_decode_range(KBD_ARP_LOCAL_TRANSPOSE, -12, 12);
  cfg.spread_percent = _shadow_arp_u8(KBD_ARP_LOCAL_SPREAD);
  cfg.direction_behavior = _shadow_arp_u8(KBD_ARP_LOCAL_DIRECTION_BEHAV);
  cfg.sync_mode = (arp_sync_mode_t)(_shadow_arp_u8(KBD_ARP_LOCAL_SYNC_MODE) % ARP_SYNC_COUNT);

  const bool was_enabled = s_arp_config.enabled;
  const bool was_hold = s_arp_config.hold_enabled;
  if (memcmp(&cfg, &s_arp_config, sizeof(cfg)) != 0) {
    s_arp_config = cfg;
    arp_set_config(&s_arp_engine, &s_arp_config);
    if (!cfg.enabled && was_enabled) {
      arp_stop_all(&s_arp_engine);
    }
    if (cfg.hold_enabled != was_hold) {
      arp_set_hold(&s_arp_engine, cfg.hold_enabled);
    }
  }
}

/* ================================ Bridge ================================= */

void ui_keyboard_bridge_init(void) {
  /* Init app avec sink direct (chemin court vers midi.c) */
  s_last_group_stamp = 0; // --- ARP FIX: reset burst timestamp ---
  s_last_group_seen = 0;
  ui_keyboard_app_init(&g_sink);

  memset(&s_arp_config, 0, sizeof(s_arp_config));
  arp_init(&s_arp_engine, &s_arp_config);
  arp_set_callbacks(&s_arp_engine, &k_arp_callbacks);

  _sync_arp_config_from_ui();
  ui_backend_shadow_set(KBD_UI_ID(KBD_UI_LOCAL_ARP), (uint8_t)(s_arp_config.enabled ? 1u : 0u)); // --- ARP: miroir legacy ---

  /* Lecture initiale via shadow UI */
  const uint8_t root_idx   = (uint8_t)(_shadow_ui_local(KBD_UI_LOCAL_ROOT)  & 0x7Fu); /* 0..11 */
  const uint8_t scale_idx  = (uint8_t)(_shadow_ui_local(KBD_UI_LOCAL_SCALE) & 0x1Fu);
  const bool    omni       = (_shadow_ui_local(KBD_OMNICHORD_ID) != 0);

  /* Page 2 (si non branchée, valeurs lues = 0) */
  const uint8_t order_val  = (uint8_t)(_shadow_ui_local(KBD_NOTE_ORDER_ID) & 0x03u);
  const bool    override   = (_shadow_ui_local(KBD_CHORD_OVERRIDE_ID) != 0);

  /* Root MIDI base: C4 (60) + offset root depuis vitrine */
  const uint8_t root_midi = (uint8_t)(60u + (root_idx % 12u));

  /* Appliquer à l’app + mapper + LEDs */
  ui_keyboard_app_set_params(root_midi, _map_scale_from_ui(scale_idx), omni);
  ui_keyboard_app_set_note_order((order_val == 1) ? NOTE_ORDER_FIFTHS : NOTE_ORDER_NATURAL);
  ui_keyboard_app_set_chord_override(override);

  kbd_input_mapper_init(omni); /* état initial */
  // --- FIX: ne pas écraser le mode LED SEQ au démarrage (on laisse le bridge LED décider) ---
  ui_led_backend_set_keyboard_omnichord(omni);
}

void ui_keyboard_bridge_update_from_model(void) {
  /* Lecture fréquente (chaque itération de la boucle UI) */
  const uint8_t root_idx   = (uint8_t)(_shadow_ui_local(KBD_UI_LOCAL_ROOT)  & 0x7Fu);
  const uint8_t scale_idx  = (uint8_t)(_shadow_ui_local(KBD_UI_LOCAL_SCALE) & 0x1Fu);
  const bool    omni       = (_shadow_ui_local(KBD_OMNICHORD_ID) != 0);

  const uint8_t order_val  = (uint8_t)(_shadow_ui_local(KBD_NOTE_ORDER_ID) & 0x03u);
  const bool    override   = (_shadow_ui_local(KBD_CHORD_OVERRIDE_ID) != 0);

  const uint8_t root_midi  = (uint8_t)(60u + (root_idx % 12u));

  /* MAJ immédiate de l’app (idempotent) */
  ui_keyboard_app_set_params(root_midi, _map_scale_from_ui(scale_idx), omni);
  ui_keyboard_app_set_note_order((order_val == 1) ? NOTE_ORDER_FIFTHS : NOTE_ORDER_NATURAL);
  ui_keyboard_app_set_chord_override(override);

  /* MAJ du mapper + LEDs (idempotents) */
  kbd_input_mapper_set_omnichord_state(omni);
  ui_led_backend_set_keyboard_omnichord(omni);

  _sync_arp_config_from_ui();
  ui_backend_shadow_set(KBD_UI_ID(KBD_UI_LOCAL_ARP), (uint8_t)(s_arp_config.enabled ? 1u : 0u));
}

void ui_keyboard_bridge_tick(systime_t now) {
  (void)now;
  ui_keyboard_app_tick(0u);
  arp_tick(&s_arp_engine, now);
}

void ui_keyboard_bridge_on_transport_stop(void) {
  arp_stop_all(&s_arp_engine); // --- ARP: STOP transport ---
}
