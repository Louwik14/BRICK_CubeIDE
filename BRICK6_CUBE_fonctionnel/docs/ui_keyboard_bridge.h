/**
 * @file ui_keyboard_bridge.h
 * @brief Pont vitrine UI ↔ app Keyboard ↔ backend (lecture via shadow UI, latence minimale).
 * @ingroup ui_apps
 */

#ifndef BRICK_UI_KEYBOARD_BRIDGE_H
#define BRICK_UI_KEYBOARD_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "apps/rtos_shim.h" // --- ARP: systime_t pour tick ---

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise l’app Keyboard et connecte le sink MIDI (via ui_backend_param_changed).
 *
 * - Lecture initiale des paramètres depuis le **shadow UI** (root/scale/omni + page 2).
 * - Initialisation du mapper selon Omnichord.
 * - Publication du mode LED Keyboard.
 */
void ui_keyboard_bridge_init(void);

/**
 * @brief Synchronise les paramètres UI (Root, Scale, Omnichord, Note Order, Chord Override) vers l’app.
 *
 * @details
 * Lecture **via shadow UI** (idempotent, très fréquent) → mise à jour immédiate
 * de l’app, du mapper et des LEDs si nécessaire.
 */
void ui_keyboard_bridge_update_from_model(void);

/**
 * @brief Tick optionnel (placeholder pour intégrations futures, ex: ARP).
 */
void ui_keyboard_bridge_tick(systime_t now); // --- ARP: tick haute résolution ---

/**
 * @brief Panic clavier/ARP lors d'un STOP transport.
 */
void ui_keyboard_bridge_on_transport_stop(void); // --- ARP: flush note engine ---

#ifdef __cplusplus
}
#endif

#endif /* BRICK_UI_KEYBOARD_BRIDGE_H */
